"""
Aerofly Reader SDK - Exceptions

Custom exceptions for the SDK.
"""


class AeroflyError(Exception):
    """Base exception for all Aerofly Reader SDK errors."""
    pass


class ConnectionError(AeroflyError):
    """Error connecting to the Aerofly Reader DLL.

    Raised when:
    - The TCP connection cannot be established
    - The connection is refused (DLL not running)
    - Connection timeout occurs
    """
    pass


class DisconnectedError(AeroflyError):
    """Connection to Aerofly Reader was lost.

    Raised when:
    - The server closes the connection
    - Network error during communication
    """
    pass


class TimeoutError(AeroflyError):
    """Timeout waiting for data from Aerofly Reader.

    Raised when:
    - No data received within the specified timeout
    - Read operation times out
    """
    pass


class DataError(AeroflyError):
    """Error in the received data.

    Raised when:
    - JSON parsing fails
    - Data structure is invalid
    - Required fields are missing
    """
    pass


class SharedMemoryError(AeroflyError):
    """Error accessing shared memory.

    Raised when:
    - Shared memory mapping cannot be opened
    - Memory access fails
    - Platform not supported (non-Windows)
    """
    pass


class NotConnectedError(AeroflyError):
    """Operation attempted without an active connection.

    Raised when:
    - read() or stream() called before connect()
    - Client is in disconnected state
    """
    pass
