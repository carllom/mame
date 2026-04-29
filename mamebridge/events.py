"""Event subscription and async queue plumbing."""

from __future__ import annotations

import asyncio
from typing import Any, AsyncIterator

from mamebridge.protocol import Event


class EventBus:
    """Broadcast incoming notifications to multiple async subscribers.

    The reader task pushes events here; subscribers get filtered async iterators.
    Queues are bounded to prevent unbounded memory growth if a subscriber stops
    consuming.
    """

    def __init__(self, max_queue_size: int = 1000):
        self._subscribers: list[tuple[str | None, asyncio.Queue[Event]]] = []
        self._max_queue_size = max_queue_size

    def push(self, event: Event) -> None:
        """Push an event to all matching subscribers."""
        for filt, queue in self._subscribers:
            if filt is None or filt == event.method:
                try:
                    queue.put_nowait(event)
                except asyncio.QueueFull:
                    # Drop oldest to make room
                    try:
                        queue.get_nowait()
                    except asyncio.QueueEmpty:
                        pass
                    try:
                        queue.put_nowait(event)
                    except asyncio.QueueFull:
                        pass

    def subscribe(self, name: str | None = None) -> _Subscription:
        """Create a new subscription. Use as an async iterator."""
        queue: asyncio.Queue[Event] = asyncio.Queue(maxsize=self._max_queue_size)
        entry = (name, queue)
        self._subscribers.append(entry)
        return _Subscription(queue, self._subscribers, entry)

    async def wait_for(
        self,
        name: str,
        *,
        timeout: float = 10.0,
        filter: Any = None,  # callable(Event) -> bool
    ) -> Event:
        """Wait for a single matching event.

        Args:
            name: Event method name to match.
            timeout: Seconds to wait before raising TimeoutError.
            filter: Optional predicate for additional filtering.
        """
        from mamebridge.errors import TimeoutError

        sub = self.subscribe(name)
        try:
            deadline = asyncio.get_event_loop().time() + timeout
            async for event in sub:
                if filter is not None and not filter(event):
                    remaining = deadline - asyncio.get_event_loop().time()
                    if remaining <= 0:
                        raise TimeoutError(f"timed out waiting for {name}")
                    continue
                return event
                # Unreachable but makes type checker happy
        except asyncio.TimeoutError:
            raise TimeoutError(f"timed out waiting for {name}")
        finally:
            sub.close()
        raise TimeoutError(f"timed out waiting for {name}")  # pragma: no cover


class _Subscription:
    """An async iterator over events for a single subscriber."""

    def __init__(
        self,
        queue: asyncio.Queue[Event],
        subscribers: list[tuple[str | None, asyncio.Queue[Event]]],
        entry: tuple[str | None, asyncio.Queue[Event]],
    ):
        self._queue = queue
        self._subscribers = subscribers
        self._entry = entry

    def __aiter__(self) -> AsyncIterator[Event]:
        return self

    async def __anext__(self) -> Event:
        try:
            return await self._queue.get()
        except asyncio.CancelledError:
            raise StopAsyncIteration

    def close(self) -> None:
        """Unsubscribe and stop receiving events."""
        try:
            self._subscribers.remove(self._entry)
        except ValueError:
            pass
