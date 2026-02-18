// Linux tiny HTTP server.
// Nicole Hamilton  nham@umich.edu

// This variation of LinuxTinyServer supports a simple plugin interface
// to allow "magic paths" to be intercepted.  (But the autograder will
// not test this feature.)

// Usage:  LinuxTinyServer port rootdirectory

// Compile with g++ -pthread LinuxTinyServer.cpp -o LinuxTinyServer
// To run under WSL (Windows Subsystem for Linux), you may have to
// elevate with sudo if the bind fails.

// LinuxTinyServer does not look for default index.htm or similar
// files.  If it receives a GET request on a directory, it will refuse
// it, returning an HTTP 403 error, access denied.  This could be
// improved.

// It also does not support HTTP Connection: keep-alive requests and
// will close the socket at the end of each response.  This is a
// perf issue, forcing the client browser to reconnect for each
// request and a candidate for improvement.


#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>
#include <string>
#include <cassert>

using namespace std;


 // The constructor for any plugin should set Plugin = this so that
 // LinuxTinyServer knows it exists and can call it.

#include "Plugin.h"
PluginObject *Plugin = nullptr;


// Root directory for the website, taken from argv[ 2 ].
// (Yes, a global variable since it never changes.)

char *RootDirectory;


//  Multipurpose Internet Mail Extensions (MIME) types

struct MimetypeMap
   {
   const char *Extension, *Mimetype;
   };

const MimetypeMap MimeTable[ ] =
   {
   // List of some of the most common MIME types in sorted order.
   // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Complete_list_of_MIME_types
   ".3g2",     "video/3gpp2",
   ".3gp",     "video/3gpp",
   ".7z",      "application/x-7z-compressed",
   ".aac",     "audio/aac",
   ".abw",     "application/x-abiword",
   ".arc",     "application/octet-stream",
   ".avi",     "video/x-msvideo",
   ".azw",     "application/vnd.amazon.ebook",
   ".bin",     "application/octet-stream",
   ".bz",      "application/x-bzip",
   ".bz2",     "application/x-bzip2",
   ".csh",     "application/x-csh",
   ".css",     "text/css",
   ".csv",     "text/csv",
   ".doc",     "application/msword",
   ".docx",    "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
   ".eot",     "application/vnd.ms-fontobject",
   ".epub",    "application/epub+zip",
   ".gif",     "image/gif",
   ".htm",     "text/html",
   ".html",    "text/html",
   ".ico",     "image/x-icon",
   ".ics",     "text/calendar",
   ".jar",     "application/java-archive",
   ".jpeg",    "image/jpeg",
   ".jpg",     "image/jpeg",
   ".js",      "application/javascript",
   ".json",    "application/json",
   ".mid",     "audio/midi",
   ".midi",    "audio/midi",
   ".mpeg",    "video/mpeg",
   ".mpkg",    "application/vnd.apple.installer+xml",
   ".odp",     "application/vnd.oasis.opendocument.presentation",
   ".ods",     "application/vnd.oasis.opendocument.spreadsheet",
   ".odt",     "application/vnd.oasis.opendocument.text",
   ".oga",     "audio/ogg",
   ".ogv",     "video/ogg",
   ".ogx",     "application/ogg",
   ".otf",     "font/otf",
   ".pdf",     "application/pdf",
   ".png",     "image/png",
   ".ppt",     "application/vnd.ms-powerpoint",
   ".pptx",    "application/vnd.openxmlformats-officedocument.presentationml.presentation",
   ".rar",     "application/x-rar-compressed",
   ".rtf",     "application/rtf",
   ".sh",      "application/x-sh",
   ".svg",     "image/svg+xml",
   ".swf",     "application/x-shockwave-flash",
   ".tar",     "application/x-tar",
   ".tif",     "image/tiff",
   ".tiff",    "image/tiff",
   ".ts",      "application/typescript",
   ".ttf",     "font/ttf",
   ".vsd",     "application/vnd.visio",
   ".wav",     "audio/x-wav",
   ".weba",    "audio/webm",
   ".webm",    "video/webm",
   ".webp",    "image/webp",
   ".woff",    "font/woff",
   ".woff2",   "font/woff2",
   ".xhtml",   "application/xhtml+xml",
   ".xls",     "application/vnd.ms-excel",
   ".xlsx",    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
   ".xml",     "application/xml",
   ".xul",     "application/vnd.mozilla.xul+xml",
   ".zip",     "application/zip"
   };


const char *Mimetype( const string filename )
   {
   // Return the Mimetype associated with any extension on the filename.

   //    YOUR CODE HERE


   // Anything not matched is an "octet-stream", treated as
   // an unknown binary, which browsers treat as a download.

   // parse
   string extension;
   size_t ext_pos = filename.find_last_of('.');
   if (ext_pos != string::npos) {
      extension = filename.substr(ext_pos);
      // lowercase that jawn
      for (auto &c : extension) {
         c = (char) tolower(c);
      }
   }
   else {
      return "application/octet-stream";
   }

   // search
   size_t num_entries = sizeof(MimeTable) / sizeof(MimeTable[0]);

   for (size_t i = 0; i < num_entries; ++i) {
      if (MimeTable[i].Extension == extension) {
         return MimeTable[i].Mimetype;
      }
   }

   return "application/octet-stream";
   }


int HexLiteralCharacter( char c )
   {
   // If c contains the Ascii code for a hex character, return the
   // binary value; otherwise, -1.

   int i;

   if ( '0' <= c && c <= '9' )
      i = c - '0';
   else
      if ( 'a' <= c && c <= 'f' )
         i = c - 'a' + 10;
      else
         if ( 'A' <= c && c <= 'F' )
            i = c - 'A' + 10;
         else
            i = -1;

   return i;
   }


string UnencodeUrlEncoding( string &path )
   {
   // Unencode any %xx encodings of characters that can't be
   // passed in a URL.

   // (Unencoding can only shorten a string or leave it unchanged.
   // It never gets longer.)

   const char *start = path.c_str( ), *from = start;
   string result;
   char c, d;


   while ( ( c = *from++ ) != 0 )
      if ( c == '%' )
         {
         c = *from;
         if ( c )
            {
            d = *++from;
            if ( d )
               {
               int i, j;
               i = HexLiteralCharacter( c );
               j = HexLiteralCharacter( d );
               if ( i >= 0 && j >= 0 )
                  {
                  from++;
                  result += ( char )( i << 4 | j );
                  }
               else
                  {
                  // If the two characters following the %
                  // aren't both hex digits, treat as
                  // literal text.

                  result += '%';
                  from--;
                  }
               }
            }
         }
      else
         result += c;

   return result;
   }


bool SafePath( const char *path )
   {
   // Watch out for paths containing .. segments that
   // attempt to go higher than the root directory
   // for the website.

   // The path must start with a /.

   if ( *path != '/' )
      return false;

   // Return false for any path containing .. segments that
   // attempt to go higher than the root directory for the
   // website.

   //    YOUR CODE HERE
   // auto dots = strstr(str, )
   auto ptr = path;
   while (*ptr != '\0') {
      if (*ptr == '.') {
         if (*(ptr + 1) == '.') {
            return false;
         }
      }
      ptr++;
   }

   return true;
   }


off_t FileSize( int f )
   {
   // Return -1 for directories.

   struct stat fileInfo;
   fstat( f, &fileInfo );
   if ( ( fileInfo.st_mode & S_IFMT ) == S_IFDIR )
      return -1;
   return fileInfo.st_size;
   }


void AccessDenied( int talkSocket )
   {
   cerr << "AccessDenied()" << endl;

   const char accessDenied[ ] = "HTTP/1.1 403 Access Denied\r\n"
         "Content-Length: 0\r\n"
         "Connection: close\r\n\r\n";

   cout << accessDenied;
   send( talkSocket, accessDenied, sizeof( accessDenied ) - 1, 0 );
   }


void FileNotFound( int talkSocket )
   {
   cerr << "FileNotFound()" << endl;

   const char fileNotFound[ ] = "HTTP/1.1 404 Not Found\r\n"
         "Content-Length: 0\r\n"
         "Connection: close\r\n\r\n";

   cout << fileNotFound;
   send( talkSocket, fileNotFound, sizeof( fileNotFound ) - 1, 0 );
   }


ssize_t SendAll(int talkSocket, const char* msg, size_t total) {
   size_t sent = 0;
   ssize_t bytes = 0;
   while (sent < total) {
      bytes = send(talkSocket, msg + sent, total - sent, 0 );
      if (bytes == -1) {
         // TODO: handle error codes
         return -1;
      }
      if (bytes == 0) {
         break;
      }

      sent += size_t(bytes);
   }

   return sent;
}

ssize_t RecvAll(int talkSocket, char* file_buf, size_t buf_size) {
   size_t recvd = 0;
   ssize_t bytes = 0;

   while (recvd < buf_size - 1) {
      bytes = recv(talkSocket, file_buf + recvd, buf_size - recvd - 1, 0 );
      if (bytes == -1) {
         // TODO: handle error codes
         return -1;
      }
      if (bytes == 0) {
         break;
      }
      
      recvd += size_t(bytes);
      file_buf[recvd] = '\0';
      
      if (strstr(file_buf, "\r\n\r\n")) {
         break;
      }
   }

   return recvd;
}


void *Talk( void *talkSocket )
   {
   // look for a GET message, then reply with the
   // requested file.

   // Cast from void * to int * to recover the talk socket id
   // then delete the copy passed on the heap.

   // Read the request from the socket and parse it to extract
   // the action and the path, unencoding any %xx encodings.

   // Check to see if there's a plugin and, if there is,
   // whether this is a magic path intercepted by the plugin.

   // If it is intercepted, call the plugin's ProcessRequest( )
   // and send whatever's returned over the socket.

   // If it isn't intercepted, action must be "GET" and
   // the path must be safe.

   // If the path refers to a directory, access denied.
   // If the path refers to a file, write it to the socket.

   // Close the socket and return nullptr.


   //    YOUR CODE HERE

   // listen
   char* buf = new char[10000];
   int *sockPtr = (int *)(talkSocket);
   int socket = *sockPtr;
   delete sockPtr;

   size_t bytes = RecvAll(socket, buf, 10000 - 1);
   buf[bytes] = '\0';

   string request = string(buf);
   
   // cleanup
   delete[] buf;
   buf = nullptr;

   // parse: action, path
   size_t space1 = request.find(' ');
   size_t space2 = request.find(' ', space1 + 1);

   if (space1 == string::npos || space2 == string::npos) {
      close(socket);
      return nullptr;
   }
   
   string action = request.substr(0, space1);
   string path = request.substr(space1 + 1, space2 - space1 - 1);

   // unencode
   path = UnencodeUrlEncoding(path);
   
   // plugin
   if (Plugin && Plugin->MagicPath(path)) {
      string response = Plugin->ProcessRequest(request);

      // HEADER INCLUDED BY PLUGIN
      // send response
      SendAll(socket, response.c_str(), response.length());

      // close
      close(socket);
      return nullptr;
   }
   
   
   if (action != "GET" || !SafePath(path.c_str())) {
      // error handling?
      AccessDenied(socket);
      // close
      close(socket);
      return nullptr;

   }

   // open(path, flags (reading only))
   string full_path = string(RootDirectory) + path;
   cerr << "full path: " << full_path << endl;
   int file = open(full_path.c_str(), O_RDONLY);

   // file not found
   if (file == -1) {
      FileNotFound(socket);
      // close
      close(socket);
      return nullptr;
   }

   // directory
   off_t fileSize = FileSize(file);
   if (fileSize == -1) {
      AccessDenied(socket);
      // close
      close(socket);
      return nullptr;
   }


   // send header
   string header = "HTTP/1.1 200 OK\r\n";
   header += "Content-Type: " + string(Mimetype(path)) + "\r\n"; 
   header += "Content-Length: " + to_string(fileSize) + "\r\n";
   header += "Connection: close\r\n\r\n";

   SendAll(socket, header.c_str(), header.length());

   // response: file at path
   char* file_buf = new char[10000];
   bytes = 0;
   while (true) {
      bytes = read(file, file_buf, 10000);

      // failure to read
      if (bytes < 1) {
         // eof: 0
         // error: -1
         break;
      }

      // failure to send
      if (SendAll(socket, file_buf, bytes) == -1) {
         break;
      }
   }

   delete[] file_buf;
   file_buf = nullptr;

   // close
   close(file);
   close(socket);
   return nullptr;

   }



int main( int argc, char **argv )
   {
   if ( argc != 3 )
      {
      cerr << "Usage:  " << argv[ 0 ] << " port rootdirectory" << endl;
      return 1;
      }

   int port = atoi( argv[ 1 ] );
   RootDirectory = argv[ 2 ];

   // Discard any trailing slash.  (Any path specified in
   // an HTTP header will have to start with /.)

   char *r = RootDirectory;
   if ( *r )
      {
      do
         r++;
      while ( *r );
      r--;
      if ( *r == '/' )
         *r = 0;
      }

   // We'll use two sockets, one for listening for new
   // connection requests, the other for talking to each
   // new client.

   int listenSocket, talkSocket;

   // Create socket address structures to go with each
   // socket.

   struct sockaddr_in listenAddress,  talkAddress;
   socklen_t talkAddressLength = sizeof( talkAddress );
   memset( &listenAddress, 0, sizeof( listenAddress ) );
   memset( &talkAddress, 0, sizeof( talkAddress ) );

   // Fill in details of where we'll listen.

   // We'll use the standard internet family of protocols.
   listenAddress.sin_family = AF_INET;

   // htons( ) transforms the port number from host (our)
   // byte-ordering into network byte-ordering (which could
   // be different).
   listenAddress.sin_port = htons( port );

   // INADDR_ANY means we'll accept connections to any IP
   // assigned to this machine.
   listenAddress.sin_addr.s_addr = htonl( INADDR_ANY );

   // Create the listenSocket, specifying that we'll r/w
   // it as a stream of bytes using TCP/IP.
   //    YOUR CODE HERE
   listenSocket = socket(AF_INET, SOCK_STREAM, 0);


   // Bind the listen socket to the IP address and protocol
   // where we'd like to listen for connections.
   //    YOUR CODE HERE
   cout << "bind()" << endl;
   if (::bind(listenSocket, (struct sockaddr *)&listenAddress, sizeof(listenAddress)) == -1) {
      cerr << "bind() returned error" << endl;
   }


   // Begin listening for clients to connect to us.
   //    YOUR CODE HERE
   cout << "listen()" << endl;
   listen(listenSocket, SOMAXCONN);
   // The second argument to listen( ) specifies the maximum
   // number of connection requests that can be allowed to
   // stack up waiting for us to accept them before Linux
   // starts refusing or ignoring new ones.
   //
   // SOMAXCONN is a system-configured default maximum socket
   // queue length.  (Under WSL Ubuntu, it's defined as 128
   // in /usr/include/x86_64-linux-gnu/bits/socket.h.)

   
   // Accept each new connection and create a thread to talk with
   // the client over the new talk socket that's created by Linux
   // when we accept the connection.
   //    YOUR CODE HERE
   while (true) {
      talkSocket = accept(listenSocket, 
         (struct sockaddr *)&talkAddress, 
         &talkAddressLength);
      
      cout << "accept() returned" << endl;
      // When creating a child thread, you get to pass a void *,
      // usually used as a pointer to an object with whatever
      // information the child needs.

      // The talk socket is passed on the heap rather than with a
      // pointer to the local variable because we're going to quickly
      // overwrite that local variable with the next accept( ).  Since
      // this is multithreaded, we can't predict whether the child will
      // run before we do that.  The child will be responsible for
      // freeing the resource.  We do not wait for the child thread
      // to complete.
      //
      // (A simpler alternative in this particular case would be to
      // caste the int talksocket to a void *, knowing that a void *
      // must be at least as large as the int.  But that would not
      // demonstrate what to do in the general case.)

      
      //    YOUR CODE HERE
      int *talkSockPtr = new int(talkSocket);
      pthread_t thread1;

      pthread_create( &thread1, nullptr, Talk, talkSockPtr);
      cerr << "thread created" << endl;

      }

   close( listenSocket );
   }
