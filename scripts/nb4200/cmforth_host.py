#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:Carl Lom
"""Terminal and serial disk host for cmFORTH on the Novix NB4200 (MAME nb4200).

cmFORTH has no disk of its own. The machine at the other end of its RS-232
line is both its terminal and its disk, sharing the one serial line:

  - normal traffic is terminal text: keys go to cmFORTH, output is displayed
  - cmFORTH starts a disk request by sending 03, then the block number
    (high byte first). Bit 15 set means write.
      read:  the host sends the 1024 bytes of the block
      write: cmFORTH sends 1024 bytes, then waits for a key as acknowledgement
  - blocks live in a block file: 1024 bytes per block, block n at offset n*1024

Run this first, then start MAME with the null modem connected to it:

  cmforth_host.py [--blocks cmforth.blk] [--port 4321]
  mame nb4200 -rs232 null_modem -bitb socket.127.0.0.1:4321

After reset cmFORTH measures the baud rate from the first key; this script
sends a 'B' when MAME connects (disable with --no-autobaud). Type Ctrl-] to quit.

With --type, the given lines are typed one by one and the script exits once
the output goes quiet, which is handy for testing.
"""

import argparse
import os
import select
import socket
import sys
import time

DISK_ESCAPE = 0x03
BLOCK_SIZE = 1024
QUIT_KEY = 0x1d  # Ctrl-]


class BlockFile:
	def __init__(self, path):
		self.path = path
		if not os.path.exists(path):
			open(path, 'wb').close()

	def read(self, block):
		with open(self.path, 'rb') as f:
			f.seek(block * BLOCK_SIZE)
			data = f.read(BLOCK_SIZE)
		# blocks past the end of the file read as spaces
		return data + b' ' * (BLOCK_SIZE - len(data))

	def write(self, block, data):
		with open(self.path, 'r+b') as f:
			f.seek(0, os.SEEK_END)
			end = f.tell()
			if end < block * BLOCK_SIZE:
				f.write(b' ' * (block * BLOCK_SIZE - end))
			f.seek(block * BLOCK_SIZE)
			f.write(data)


class Host:
	def __init__(self, conn, blocks, log):
		self.conn = conn
		self.blocks = blocks
		self.log = log
		self.pending = bytearray()

	def recv_exact(self, count):
		while len(self.pending) < count:
			data = self.conn.recv(4096)
			if not data:
				raise ConnectionError('MAME closed the connection')
			self.pending.extend(data)
		data = bytes(self.pending[:count])
		del self.pending[:count]
		return data

	def disk_request(self):
		hi, lo = self.recv_exact(2)
		number = (hi << 8) | lo
		block = number & 0x7fff
		if number & 0x8000:
			data = self.recv_exact(BLOCK_SIZE)
			self.blocks.write(block, data)
			self.log(f'[write block {block}]')
			self.conn.sendall(b'\r')  # the key cmFORTH waits for after a write
		else:
			self.log(f'[read block {block}]')
			self.conn.sendall(self.blocks.read(block))

	def pump(self, output):
		"""Handle everything MAME has sent; terminal text goes to output."""
		data = self.conn.recv(4096)
		if not data:
			raise ConnectionError('MAME closed the connection')
		self.pending.extend(data)
		while self.pending:
			byte = self.pending.pop(0)
			if byte == DISK_ESCAPE:
				self.disk_request()
			else:
				output(bytes([byte]))


def main():
	parser = argparse.ArgumentParser(description='Terminal and serial disk host for cmFORTH on the NB4200')
	parser.add_argument('--port', type=int, default=4321, help='TCP port MAME connects to (default 4321)')
	parser.add_argument('--blocks', default='cmforth.blk', help='block file (default cmforth.blk)')
	parser.add_argument('--no-autobaud', action='store_true', help="don't send the 'B' cmFORTH calibrates on")
	parser.add_argument('--type', nargs='+', metavar='LINE', help='type these lines, then exit when output goes quiet')
	parser.add_argument('--char-delay', type=float, default=0.03, help='seconds between typed characters (default 0.03)')
	parser.add_argument('--quiet-time', type=float, default=3.0, help='seconds of silence that end --type mode (default 3)')
	args = parser.parse_args()

	interactive = args.type is None and sys.stdin.isatty()
	out = sys.stdout.buffer

	def write_out(data):
		out.write(data)
		out.flush()

	def log(message):
		write_out(f'\r\n{message}\r\n'.encode() if interactive else f'{message}\n'.encode())

	server = socket.socket()
	server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	server.bind(('127.0.0.1', args.port))
	server.listen(1)
	print(f'Waiting for MAME: mame nb4200 -rs232 null_modem -bitb socket.127.0.0.1:{args.port}', file=sys.stderr)
	conn, _ = server.accept()
	server.close()
	host = Host(conn, BlockFile(args.blocks), log)
	print('Connected' + (', Ctrl-] quits' if interactive else ''), file=sys.stderr)

	def wait(seconds):
		end = time.time() + seconds
		while (remaining := end - time.time()) > 0:
			if select.select([conn], [], [], remaining)[0]:
				host.pump(write_out)

	def type_text(text):
		# one character at a time, so cmFORTH can echo each before the next arrives
		for ch in text.encode('latin-1'):
			conn.sendall(bytes([ch]))
			wait(args.char_delay)

	try:
		wait(0.5)
		if not args.no_autobaud:
			type_text('B')
		if args.type is not None:
			wait(1.0)
			for line in args.type:
				type_text(line + '\r')
				wait(0.2)
				# let a LOAD or other long-running line finish before typing the next one
				last = time.time()
				while time.time() - last < args.quiet_time:
					if select.select([conn], [], [], 0.1)[0]:
						host.pump(write_out)
						last = time.time()
			write_out(b'\n')
			return

		saved = None
		if interactive:
			import termios
			import tty
			saved = termios.tcgetattr(sys.stdin)
			tty.setraw(sys.stdin)
		try:
			while True:
				ready = select.select([conn, sys.stdin], [], [])[0]
				if conn in ready:
					host.pump(write_out)
				if sys.stdin in ready:
					keys = os.read(sys.stdin.fileno(), 1024)
					if not keys or QUIT_KEY in keys:
						break
					type_text(keys.replace(b'\n', b'\r').decode('latin-1'))
		finally:
			if saved is not None:
				termios.tcsetattr(sys.stdin, termios.TCSADRAIN, saved)
	except ConnectionError as e:
		print(f'\n{e}', file=sys.stderr)
	finally:
		conn.close()


if __name__ == '__main__':
	main()
