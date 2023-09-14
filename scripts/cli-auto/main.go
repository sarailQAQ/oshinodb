package main

import (
	"fmt"
	"github.com/sarailqaq/oshinodb/scripts/cli-auto/client"
	"log"
	"time"
)

func main() {
	log.SetFlags(log.Ldate | log.Ltime | log.Lshortfile)

	cli, _ := client.NewCli("127.0.0.1:8765")

	table := "test"
	_ = cli.Exec("drop table test;")

	_ = cli.Exec("create table test(w_id int,name char(8), flo float);")

	_ = cli.Exec(fmt.Sprintf("create index %s(w_id,name);", table))


	n := 10000
	for i := 1; i <= n; i += 1 {
		_ = cli.Exec(fmt.Sprintf("insert into %s values(%d, '%s', 1.0);", table, i, "12345678"))
	}

	begin := time.Now()
	for i := 1; i <= n; i += 1 {
		_ = cli.Exec(fmt.Sprintf("select * from %s where w_id = %d and name='12345678';", table, i))
	}
	fmt.Println(time.Now().Sub(begin).String())
//
//
//
// 	time.Sleep(time.Second)
// 	begin = time.Now()
// 	for i := 1; i <= n; i += 1 {
// 		_ = cli.Exec(fmt.Sprintf("select * from %s where w_id = %d;", table, i))
// 	}
// 	fmt.Println(time.Now().Sub(begin).String())
}
