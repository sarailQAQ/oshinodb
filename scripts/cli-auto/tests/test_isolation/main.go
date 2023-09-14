package main

import (
	"fmt"
	"github.com/sarailqaq/oshinodb/scripts/cli-auto/client"
	"log"
)

func main() {
	log.SetFlags(log.Ldate | log.Ltime | log.Lshortfile)

	cli, err := client.NewCli("127.0.0.1:8765")
	if err != nil {
		log.Fatal(err)
	}

	cli.Exec("drop table concurrency_test;")
	if err != nil {
		log.Fatal(err)
	}

	cli.Exec("create table concurrency_test (id int, name char(8), score float);")
	if err != nil {
		log.Fatal(err)
	}

	cli.Exec("insert into concurrency_test values (1, 'xiaohong', 90.0);")
	cli.Exec("insert into concurrency_test values (2, 'xiaoming', 95.0);")
	cli.Exec("insert into concurrency_test values (3, 'zhanghua', 88.5);")

	cli2, err := client.NewCli("127.0.0.1:8765")
	if err != nil {
		log.Fatal(err)
	}

	cli.Exec("begin;")
	cli2.Exec("begin;")
	fmt.Println(cli.Exec("update concurrency_test set score = 100.0 where id = 2;"))
	fmt.Println(cli2.Exec("select * from concurrency_test where id = 2;"))
	cli.Exec("abort;")
	fmt.Println(cli.Exec("select * from concurrency_test where id = 2;"))
	cli2.Exec("commit;")
}
