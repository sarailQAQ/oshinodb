package client

import (
	"log"
	"net"
)

func NewCli(url string) (*Cli, error) {
	conn, err := net.Dial("tcp", url)
	return &Cli{conn: conn}, err
}

type Cli struct {
	conn net.Conn
}

func (cli *Cli) Exec(msg string) (resp string) {
	_, err := cli.conn.Write([]byte(msg))
	if err != nil {
		log.Fatal(err)
	}

	data := make([]byte, 2048)
	_, err = cli.conn.Read(data)
	if err != nil {
		log.Fatal(err)
	}

	return string(data)
}
