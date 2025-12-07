# Http_client_server-program-in-C
CHLP Client-Server

A lightweight custom client–server system implementing a simple protocol named CHLP/1.0.
The server supports GET, POST, and ECHO methods, handles file serving, accepts uploads, and responds with structured headers.
The client can send requests using the same protocol and display server responses.

Features
Server

Multithreaded client handling using pthread

Serves static files from the www/ directory

Receives uploads and stores them in uploads/

Implements three custom methods:

GET /path – returns a file from www/

POST /path – writes request body to uploads/post_<timestamp>_<rand>.txt

ECHO /path – returns body back to the client

Uses a custom header format:

CHLP/1.0 <status message>
Body-Size: <bytes>

<body>

Client

Supports sending GET, POST, and ECHO requests

Reads a response status line, headers, and body

Sends correct Body-Size header automatically

Resolves hostnames using gethostbyname() fallback

Safe send/receive using write_n() and read_n()

Reads request body from file for POST/ECHO

Directory Structure
project/
│
├── server.c
├── client.c
├── www/           # server GET files live here
├── uploads/       # server writes POST bodies here
└── README.md

Building

Compile both programs:

gcc -pthread server.c -o server
gcc client.c -o client

Running the Server

By default, the server listens on port 8080.

./server


Or specify a custom port:

./server 9090


When running, it will automatically create:

www/ (if missing)

uploads/ (if missing)

Using the Client
1. GET request

Fetch a file from the server:

./client 127.0.0.1 8080 GET /index.html

2. POST request

Send a body from a file:

./client 127.0.0.1 8080 POST /upload uploads/mydata.txt


The server stores the body in:

uploads/post_<timestamp>_<rand>.txt

3. ECHO request

Send a file's content and get it echoed back:

./client 127.0.0.1 8080 ECHO /anything body.txt

Protocol Format
Request Example
GET /index.html CHLP/1.0
Body-Size: 0


Response Example
CHLP/1.0 200 OK
Body-Size: 57

<html> ... file contents ... </html>

Security Notes

Prevents directory traversal (../) via resource sanitization

Serves only from www/

Randomized file names for uploads

Does not implement MIME types (all responses are raw bytes)

Not a full HTTP implementation (intentionally lightweight)

Known Limitations

Only supports IPv4

No keep-alive

No chunked encoding

Very simple header parsing logic

Error handling is minimalistic

Client blocks until full body is received
