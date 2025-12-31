"""
Aerofly Reader SDK - TCP Client

Synchronous TCP client for connecting to AeroflyReader DLL.
"""

import socket
import json
import time
import logging
from typing import Iterator, Optional, Callable

from .models import FlightData
from .exceptions import (
    ConnectionError,
    DisconnectedError,
    TimeoutError,
    DataError,
    NotConnectedError,
)

logger = logging.getLogger(__name__)


class AeroflyClient:
    """Synchronous TCP client for AeroflyReader DLL.

    This client connects to the TCP streaming server provided by the
    AeroflyReader DLL and receives real-time flight data.

    Example:
        >>> from aerofly_reader import AeroflyClient
        >>>
        >>> # Simple usage with context manager
        >>> with AeroflyClient() as client:
        ...     for flight in client.stream():
        ...         print(f"Alt: {flight.position.altitude_ft:.0f} ft")
        ...         print(f"Speed: {flight.speeds.indicated_airspeed:.0f} kts")
        >>>
        >>> # Manual connection management
        >>> client = AeroflyClient(host='localhost', port=12345)
        >>> client.connect()
        >>> data = client.read()
        >>> print(data.aircraft_name)
        >>> client.disconnect()

    Args:
        host: Server hostname (default: 'localhost')
        port: Server port (default: 12345)
        timeout: Socket timeout in seconds (default: 5.0)
        auto_reconnect: Automatically reconnect on disconnect (default: True)
        reconnect_delay: Delay between reconnection attempts (default: 2.0)
        max_reconnect_attempts: Maximum reconnection attempts (default: 5, 0 = infinite)
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

        self._socket: Optional[socket.socket] = None
        self._buffer: str = ""
        self._connected: bool = False
        self._reconnect_count: int = 0

    @property
    def is_connected(self) -> bool:
        """Check if client is currently connected."""
        return self._connected and self._socket is not None

    def connect(self) -> None:
        """Connect to the AeroflyReader TCP server.

        Raises:
            ConnectionError: If connection cannot be established
        """
        if self._connected:
            return

        logger.info(f"Connecting to {self.host}:{self.port}...")

        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(self.timeout)
            self._socket.connect((self.host, self.port))
            self._connected = True
            self._reconnect_count = 0
            self._buffer = ""
            logger.info("Connected successfully")

        except socket.timeout:
            self._cleanup()
            raise ConnectionError(
                f"Connection timeout to {self.host}:{self.port}. "
                "Make sure Aerofly FS4 is running with AeroflyReader.dll loaded."
            )
        except socket.error as e:
            self._cleanup()
            if e.errno == 111 or "refused" in str(e).lower():
                raise ConnectionError(
                    f"Connection refused at {self.host}:{self.port}. "
                    "Make sure AeroflyReader.dll is loaded in Aerofly FS4."
                )
            raise ConnectionError(f"Connection error: {e}")

    def disconnect(self) -> None:
        """Disconnect from the server."""
        if self._socket:
            logger.info("Disconnecting...")
            self._cleanup()
            logger.info("Disconnected")

    def _cleanup(self) -> None:
        """Clean up socket resources."""
        if self._socket:
            try:
                self._socket.close()
            except Exception:
                pass
        self._socket = None
        self._connected = False
        self._buffer = ""

    def _try_reconnect(self) -> bool:
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

        time.sleep(self.reconnect_delay)

        try:
            self.connect()
            return True
        except ConnectionError as e:
            logger.warning(f"Reconnection failed: {e}")
            return False

    def read(self) -> FlightData:
        """Read the next flight data frame.

        Blocks until a complete JSON frame is received.

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
                chunk = self._socket.recv(self.BUFFER_SIZE)
                if not chunk:
                    # Server closed connection
                    self._cleanup()
                    if self._try_reconnect():
                        continue
                    raise DisconnectedError("Server closed connection")

                self._buffer += chunk.decode('utf-8')

            except socket.timeout:
                raise TimeoutError(f"No data received within {self.timeout} seconds")
            except socket.error as e:
                self._cleanup()
                if self._try_reconnect():
                    continue
                raise DisconnectedError(f"Connection lost: {e}")

    def stream(
        self,
        max_frames: Optional[int] = None,
        on_error: Optional[Callable[[Exception], bool]] = None,
    ) -> Iterator[FlightData]:
        """Stream flight data continuously.

        Yields FlightData objects as they are received from the server.
        Handles reconnection automatically if enabled.

        Args:
            max_frames: Maximum number of frames to receive (None = infinite)
            on_error: Callback for errors, return True to continue, False to stop

        Yields:
            FlightData objects

        Example:
            >>> for flight in client.stream():
            ...     print(f"Altitude: {flight.position.altitude_ft:.0f} ft")
            ...     if flight.on_ground:
            ...         break  # Stop when landed
        """
        if not self.is_connected:
            self.connect()

        frame_count = 0

        while max_frames is None or frame_count < max_frames:
            try:
                yield self.read()
                frame_count += 1

            except (DisconnectedError, TimeoutError) as e:
                if on_error and on_error(e):
                    # User wants to continue, try to reconnect
                    if self._try_reconnect():
                        continue
                raise

            except DataError as e:
                if on_error:
                    if on_error(e):
                        continue  # Skip bad frame
                    else:
                        raise
                # By default, skip bad frames and continue
                logger.warning(f"Skipping invalid frame: {e}")
                continue

    def read_one(self) -> FlightData:
        """Connect, read a single frame, and disconnect.

        Convenience method for one-shot reads.

        Returns:
            Single FlightData frame

        Example:
            >>> data = AeroflyClient().read_one()
            >>> print(f"Aircraft: {data.aircraft_name}")
        """
        was_connected = self.is_connected
        try:
            if not was_connected:
                self.connect()
            return self.read()
        finally:
            if not was_connected:
                self.disconnect()

    def __enter__(self) -> 'AeroflyClient':
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.disconnect()

    def __repr__(self) -> str:
        status = "connected" if self.is_connected else "disconnected"
        return f"AeroflyClient({self.host}:{self.port}, {status})"


def connect(
    host: str = AeroflyClient.DEFAULT_HOST,
    port: int = AeroflyClient.DEFAULT_PORT,
    **kwargs
) -> AeroflyClient:
    """Create and connect an AeroflyClient.

    Convenience function for quick connections.

    Args:
        host: Server hostname
        port: Server port
        **kwargs: Additional arguments for AeroflyClient

    Returns:
        Connected AeroflyClient instance

    Example:
        >>> with connect() as client:
        ...     data = client.read()
        ...     print(data.aircraft_name)
    """
    client = AeroflyClient(host=host, port=port, **kwargs)
    client.connect()
    return client


def stream(
    host: str = AeroflyClient.DEFAULT_HOST,
    port: int = AeroflyClient.DEFAULT_PORT,
    **kwargs
) -> Iterator[FlightData]:
    """Stream flight data from AeroflyReader.

    Convenience function for streaming data.

    Args:
        host: Server hostname
        port: Server port
        **kwargs: Additional arguments for AeroflyClient

    Yields:
        FlightData objects

    Example:
        >>> from aerofly_reader import stream
        >>>
        >>> for flight in stream():
        ...     print(f"Alt: {flight.position.altitude_ft:.0f} ft")
    """
    with AeroflyClient(host=host, port=port, **kwargs) as client:
        yield from client.stream()


def read_once(
    host: str = AeroflyClient.DEFAULT_HOST,
    port: int = AeroflyClient.DEFAULT_PORT,
    **kwargs
) -> FlightData:
    """Read a single flight data frame.

    Convenience function for one-shot reads.

    Args:
        host: Server hostname
        port: Server port
        **kwargs: Additional arguments for AeroflyClient

    Returns:
        Single FlightData frame

    Example:
        >>> from aerofly_reader import read_once
        >>> data = read_once()
        >>> print(f"Aircraft: {data.aircraft_name}")
    """
    return AeroflyClient(host=host, port=port, **kwargs).read_one()
