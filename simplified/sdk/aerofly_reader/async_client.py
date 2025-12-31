"""
Aerofly Reader SDK - Async TCP Client

Asynchronous TCP client for connecting to AeroflyReader DLL.
"""

import asyncio
import json
import logging
from typing import AsyncIterator, Optional, Callable, Awaitable

from .models import FlightData
from .exceptions import (
    ConnectionError,
    DisconnectedError,
    TimeoutError,
    DataError,
    NotConnectedError,
)

logger = logging.getLogger(__name__)


class AsyncAeroflyClient:
    """Asynchronous TCP client for AeroflyReader DLL.

    This client connects to the TCP streaming server using asyncio
    for non-blocking I/O operations.

    Example:
        >>> import asyncio
        >>> from aerofly_reader import AsyncAeroflyClient
        >>>
        >>> async def main():
        ...     async with AsyncAeroflyClient() as client:
        ...         async for flight in client.stream():
        ...             print(f"Alt: {flight.position.altitude_ft:.0f} ft")
        ...             if flight.on_ground:
        ...                 break
        >>>
        >>> asyncio.run(main())

    Args:
        host: Server hostname (default: 'localhost')
        port: Server port (default: 12345)
        timeout: Connection/read timeout in seconds (default: 5.0)
        auto_reconnect: Automatically reconnect on disconnect (default: True)
        reconnect_delay: Delay between reconnection attempts (default: 2.0)
        max_reconnect_attempts: Maximum reconnection attempts (default: 5)
    """

    DEFAULT_HOST = 'localhost'
    DEFAULT_PORT = 12345
    DEFAULT_TIMEOUT = 5.0
    BUFFER_SIZE = 8192

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        timeout: float = DEFAULT_TIMEOUT,
        auto_reconnect: bool = True,
        reconnect_delay: float = 2.0,
        max_reconnect_attempts: int = 5,
    ):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.auto_reconnect = auto_reconnect
        self.reconnect_delay = reconnect_delay
        self.max_reconnect_attempts = max_reconnect_attempts

        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._buffer: str = ""
        self._connected: bool = False
        self._reconnect_count: int = 0

    @property
    def is_connected(self) -> bool:
        """Check if client is currently connected."""
        return self._connected and self._reader is not None

    async def connect(self) -> None:
        """Connect to the AeroflyReader TCP server.

        Raises:
            ConnectionError: If connection cannot be established
        """
        if self._connected:
            return

        logger.info(f"Connecting to {self.host}:{self.port}...")

        try:
            self._reader, self._writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=self.timeout
            )
            self._connected = True
            self._reconnect_count = 0
            self._buffer = ""
            logger.info("Connected successfully")

        except asyncio.TimeoutError:
            await self._cleanup()
            raise ConnectionError(
                f"Connection timeout to {self.host}:{self.port}. "
                "Make sure Aerofly FS4 is running with AeroflyReader.dll loaded."
            )
        except OSError as e:
            await self._cleanup()
            if "refused" in str(e).lower() or e.errno == 111:
                raise ConnectionError(
                    f"Connection refused at {self.host}:{self.port}. "
                    "Make sure AeroflyReader.dll is loaded in Aerofly FS4."
                )
            raise ConnectionError(f"Connection error: {e}")

    async def disconnect(self) -> None:
        """Disconnect from the server."""
        if self._writer:
            logger.info("Disconnecting...")
            await self._cleanup()
            logger.info("Disconnected")

    async def _cleanup(self) -> None:
        """Clean up connection resources."""
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
        self._reader = None
        self._writer = None
        self._connected = False
        self._buffer = ""

    async def _try_reconnect(self) -> bool:
        """Attempt to reconnect to the server.

        Returns:
            True if reconnection successful, False otherwise
        """
        if not self.auto_reconnect:
            return False

        if self.max_reconnect_attempts > 0 and self._reconnect_count >= self.max_reconnect_attempts:
            logger.error(f"Max reconnection attempts ({self.max_reconnect_attempts}) reached")
            return False

        self._reconnect_count += 1
        logger.info(f"Reconnection attempt {self._reconnect_count}...")

        await asyncio.sleep(self.reconnect_delay)

        try:
            await self.connect()
            return True
        except ConnectionError as e:
            logger.warning(f"Reconnection failed: {e}")
            return False

    async def read(self) -> FlightData:
        """Read the next flight data frame.

        Awaits until a complete JSON frame is received.

        Returns:
            FlightData with the latest flight information

        Raises:
            NotConnectedError: If not connected
            DisconnectedError: If connection is lost
            TimeoutError: If no data received within timeout
            DataError: If received data is invalid
        """
        if not self.is_connected:
            raise NotConnectedError("Not connected. Call connect() first.")

        while True:
            # Check if we have a complete line in buffer
            if '\n' in self._buffer:
                line, self._buffer = self._buffer.split('\n', 1)
                line = line.strip()

                if line:
                    try:
                        data = json.loads(line)
                        return FlightData.from_json(data)
                    except json.JSONDecodeError as e:
                        logger.warning(f"Invalid JSON received: {e}")
                        raise DataError(f"Invalid JSON data: {e}")

            # Receive more data
            try:
                chunk = await asyncio.wait_for(
                    self._reader.read(self.BUFFER_SIZE),
                    timeout=self.timeout
                )

                if not chunk:
                    # Server closed connection
                    await self._cleanup()
                    if await self._try_reconnect():
                        continue
                    raise DisconnectedError("Server closed connection")

                self._buffer += chunk.decode('utf-8')

            except asyncio.TimeoutError:
                raise TimeoutError(f"No data received within {self.timeout} seconds")
            except OSError as e:
                await self._cleanup()
                if await self._try_reconnect():
                    continue
                raise DisconnectedError(f"Connection lost: {e}")

    async def stream(
        self,
        max_frames: Optional[int] = None,
        on_error: Optional[Callable[[Exception], Awaitable[bool]]] = None,
    ) -> AsyncIterator[FlightData]:
        """Stream flight data continuously.

        Yields FlightData objects as they are received from the server.
        Handles reconnection automatically if enabled.

        Args:
            max_frames: Maximum number of frames to receive (None = infinite)
            on_error: Async callback for errors, return True to continue

        Yields:
            FlightData objects

        Example:
            >>> async for flight in client.stream():
            ...     print(f"Altitude: {flight.position.altitude_ft:.0f} ft")
        """
        if not self.is_connected:
            await self.connect()

        frame_count = 0

        while max_frames is None or frame_count < max_frames:
            try:
                yield await self.read()
                frame_count += 1

            except (DisconnectedError, TimeoutError) as e:
                if on_error and await on_error(e):
                    if await self._try_reconnect():
                        continue
                raise

            except DataError as e:
                if on_error:
                    if await on_error(e):
                        continue
                    else:
                        raise
                logger.warning(f"Skipping invalid frame: {e}")
                continue

    async def read_one(self) -> FlightData:
        """Connect, read a single frame, and disconnect.

        Returns:
            Single FlightData frame
        """
        was_connected = self.is_connected
        try:
            if not was_connected:
                await self.connect()
            return await self.read()
        finally:
            if not was_connected:
                await self.disconnect()

    async def __aenter__(self) -> 'AsyncAeroflyClient':
        """Async context manager entry."""
        await self.connect()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        """Async context manager exit."""
        await self.disconnect()

    def __repr__(self) -> str:
        status = "connected" if self.is_connected else "disconnected"
        return f"AsyncAeroflyClient({self.host}:{self.port}, {status})"


async def async_connect(
    host: str = AsyncAeroflyClient.DEFAULT_HOST,
    port: int = AsyncAeroflyClient.DEFAULT_PORT,
    **kwargs
) -> AsyncAeroflyClient:
    """Create and connect an AsyncAeroflyClient.

    Args:
        host: Server hostname
        port: Server port
        **kwargs: Additional arguments for AsyncAeroflyClient

    Returns:
        Connected AsyncAeroflyClient instance
    """
    client = AsyncAeroflyClient(host=host, port=port, **kwargs)
    await client.connect()
    return client


async def async_stream(
    host: str = AsyncAeroflyClient.DEFAULT_HOST,
    port: int = AsyncAeroflyClient.DEFAULT_PORT,
    **kwargs
) -> AsyncIterator[FlightData]:
    """Stream flight data asynchronously.

    Args:
        host: Server hostname
        port: Server port
        **kwargs: Additional arguments for AsyncAeroflyClient

    Yields:
        FlightData objects

    Example:
        >>> async for flight in async_stream():
        ...     print(f"Alt: {flight.position.altitude_ft:.0f} ft")
    """
    async with AsyncAeroflyClient(host=host, port=port, **kwargs) as client:
        async for data in client.stream():
            yield data


async def async_read_once(
    host: str = AsyncAeroflyClient.DEFAULT_HOST,
    port: int = AsyncAeroflyClient.DEFAULT_PORT,
    **kwargs
) -> FlightData:
    """Read a single flight data frame asynchronously.

    Args:
        host: Server hostname
        port: Server port
        **kwargs: Additional arguments for AsyncAeroflyClient

    Returns:
        Single FlightData frame
    """
    return await AsyncAeroflyClient(host=host, port=port, **kwargs).read_one()
