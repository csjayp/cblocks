package main

import (
        "os"
	"strings"
        "fmt"
        "net/http"
        "net/http/fcgi"
        "net"
)
const SockAddr = "/fcgi.sock"

type FastCGIServer struct{}

func (s FastCGIServer) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	var request []string

	url := fmt.Sprintf("%v %v %v", req.Method, req.URL, req.Proto)
	fmt.Printf("%s\n", url)
	for name, headers := range req.Header {
		name = strings.ToLower(name)
		for _, h := range headers {
			request = append(request, fmt.Sprintf("%v=%v", name, h))
		}
 	}
	fmt.Printf("%s\n", strings.Join(request, "\n"))
        w.Write([]byte("This is a FastCGI example server.\n"))
}

func main() {
        fmt.Println("Starting server...")
        //l, _ := net.Listen("tcp", "127.0.0.1:9000")
        os.RemoveAll(SockAddr)
        l, r := net.Listen("unix", SockAddr)
        if r != nil {
            fmt.Printf("socket listen failed: %s\n", r)
        }
        b := new(FastCGIServer)
        fcgi.Serve(l, b)
}
