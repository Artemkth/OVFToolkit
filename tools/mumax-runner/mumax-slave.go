//go:build ignore

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"sync"
	"time"

	"github.com/mumax/3/cuda"
	. "github.com/mumax/3/engine"
)

type protocolMessage struct {
	Type     string `json:"type"`
	Token    string `json:"token,omitempty"`
	Version  string `json:"version,omitempty"`
	Protocol int    `json:"protocol,omitempty"`
	ID       int64  `json:"id,omitempty"`
	Script   string `json:"script,omitempty"`
	Value    string `json:"value,omitempty"`
	Message  string `json:"message,omitempty"`
}

var mumaxVersion = "unknown"

var (
	ipcAddress = flag.String("ipc", "", "Loopback IPC server address")
	ipcToken   = flag.String("ipc-token", "", "IPC authentication token")
	ipcEncoder *json.Encoder
	ipcLock    sync.Mutex
)

func send(message protocolMessage) error {
	ipcLock.Lock()
	defer ipcLock.Unlock()
	if ipcEncoder == nil {
		return nil
	}
	return ipcEncoder.Encode(message)
}

func EvalTicket(ticket int, symbol interface{}) {
	if err := send(protocolMessage{
		Type: "result", ID: int64(ticket), Value: fmt.Sprint(symbol),
	}); err != nil {
		LogErr("sending ticket response:", err)
	}
}

func evaluate(message protocolMessage) {
	defer func() {
		if recovered := recover(); recovered != nil {
			_ = send(protocolMessage{
				Type: "error", ID: message.ID, Message: fmt.Sprint(recovered),
			})
		}
	}()

	tree, err := World.Compile(message.Script)
	if err != nil {
		LogIn(message.Script)
		LogErr(err.Error())
		_ = send(protocolMessage{
			Type: "error", ID: message.ID, Message: err.Error(),
		})
		return
	}
	LogIn(message.Script)
	tree.Eval()
	if message.Type == "command" && message.ID != 0 {
		_ = send(protocolMessage{Type: "complete", ID: message.ID})
	}
}

func readSocket(decoder *json.Decoder) {
	for {
		var message protocolMessage
		if err := decoder.Decode(&message); err != nil {
			LogErr("IPC connection closed:", err)
			Inject <- func() { Exit() }
			return
		}

		switch message.Type {
		case "request", "command":
			request := message
			Inject <- func() { evaluate(request) }
		case "shutdown":
			Inject <- func() {
				_ = send(protocolMessage{Type: "terminating"})
				Exit()
			}
			return
		default:
			_ = send(protocolMessage{
				Type: "error", ID: message.ID,
				Message: "unknown protocol message type " + message.Type,
			})
		}
	}
}

func readStdin() {
	scanner := bufio.NewScanner(os.Stdin)
	buffer := ""
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		continuation := false
		for index := len(line); index > 0 && line[index-1] == '\\'; index-- {
			continuation = !continuation
		}
		if continuation {
			if buffer != "" {
				buffer += "\n"
			}
			buffer += line[:len(line)-1]
			continue
		}
		if buffer != "" {
			buffer += "\n"
		}
		buffer += line
		command := protocolMessage{Type: "command", Script: buffer}
		Inject <- func() { evaluate(command) }
		buffer = ""
	}
}

func main() {
	DeclFunc("EvalTicket", EvalTicket, "Return a value through the IPC ticket")
	flag.Parse()

	var connection net.Conn
	var decoder *json.Decoder
	if *ipcAddress != "" {
		if *ipcToken == "" {
			fmt.Fprintln(os.Stderr, "-ipc-token is required with -ipc")
			os.Exit(2)
		}
		var err error
		connection, err = net.DialTimeout("tcp", *ipcAddress, 10*time.Second)
		if err != nil {
			fmt.Fprintln(os.Stderr, "connecting to IPC server:", err)
			os.Exit(2)
		}
		defer connection.Close()
		ipcEncoder = json.NewEncoder(connection)
		decoder = json.NewDecoder(connection)
		if err := send(protocolMessage{
			Type: "hello", Token: *ipcToken,
			Version: mumaxVersion, Protocol: 1,
		}); err != nil {
			fmt.Fprintln(os.Stderr, "sending IPC handshake:", err)
			os.Exit(2)
		}
	}

	fmt.Printf("ASSUMING DIRECT CONTROL, on gpu #%d\n", *Flag_gpu)
	cuda.Init(*Flag_gpu)
	cuda.Synchronous = *Flag_sync
	defer Close()

	now := time.Now()
	outDir := fmt.Sprintf("mumax-%v-%02d-%02d_%02dh%02d.out",
		now.Year(), int(now.Month()), now.Day(), now.Hour(), now.Minute())
	if *Flag_od != "" {
		outDir = *Flag_od
	}
	InitIO("Mumax-slave", outDir, *Flag_forceclean)

	Eval(`SetGridSize(128, 64, 1)
SetCellSize(4e-9, 4e-9, 4e-9)
Msat = 1e6
Aex = 10e-12
alpha = 1
m = RandomMag()`)

	if *Flag_port == "" {
		fmt.Println(`//not starting GUI (-http="")`)
	} else {
		address := GoServe(*Flag_port)
		fmt.Print("//starting GUI at http://127.0.0.1", address, "\n")
	}

	fmt.Println("//Ready: Waiting for user input!")
	if decoder != nil {
		if err := send(protocolMessage{Type: "ready"}); err != nil {
			fmt.Fprintln(os.Stderr, "sending ready event:", err)
			return
		}
		go readSocket(decoder)
	}
	go readStdin()

	for {
		action := <-Inject
		action()
	}
}
