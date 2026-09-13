#include "socket.hh"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

using namespace std;

void get_URL( const string& host, const string& path )
{
  // cerr << "Function called: get_URL(" << host << ", " << path << ")\n";
  // cerr << "Warning: get_URL() has not been implemented yet.\n";

  // 1. 把"主机名"解析成"地址"（含 DNS 查询 + 端口 80）
  Address addr( host, "http" );

  // 2. 创建一个 TCP socket，并连上去
  TCPSocket sock;
  sock.connect( addr );

  // 3. 发送 HTTP 请求文本
  sock.write( "GET " + path
              + " HTTP/1.1\r\n"
                "Host: "
              + host
              + "\r\n"
                "Connection: close\r\n"
                "\r\n" );

  // 4. 循环读响应并打印，直到服务器关闭连接
  while ( not sock.eof() ) {
    string buffer;
    sock.read( buffer );
    cout << buffer;
  }
}

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
    }

    auto args = span( argv, argc );

    // The program takes two command-line arguments: the hostname and "path" part of the URL.
    // Print the usage message unless there are these two arguments (plus the program name
    // itself, so arg count = 3 in total).
    if ( argc != 3 ) {
      cerr << "Usage: " << args.front() << " HOST PATH\n";
      cerr << "\tExample: " << args.front() << " stanford.edu /class/cs144\n";
      return EXIT_FAILURE;
    }

    // Get the command-line arguments.
    const string host { args[1] };
    const string path { args[2] };

    // Call the student-written function.
    get_URL( host, path );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
