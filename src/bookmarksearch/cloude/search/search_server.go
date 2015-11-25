// 一个微博搜索的例子。
package main

import (
	"bufio"
	// "encoding/gob"
	"encoding/json"
	"flag"
	"github.com/huichen/wukong/engine"
	"github.com/huichen/wukong/types"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	// "reflect"
	// "strconv"
	"fmt"
	"strings"
	"time"
)

const (
	SecondsInADay     = 86400
	MaxTokenProximity = 2
)

var (
	searcher      = engine.Engine{}
	bms           = map[uint64]BookMark{}
	websitedata   = flag.String("weibo_data", "./data/crawler_result.dat", "网站数据文件")
	dictFile      = flag.String("dict_file", "./dict/dictionary.txt", "词典文件")
	stopTokenFile = flag.String("stop_token_file", "./dict/stop_tokens.txt", "停用词文件")
	staticFolder  = flag.String("static_folder", "static", "静态文件目录")
)

type BookMark struct {
	Id    uint64 `json:"id"`
	Owner string `json:"owner"`
	Title string `json:"title"`
	Text  string `json:"text"`
}

/*******************************************************************************
    索引
*******************************************************************************/
func indexBookMark() {
	// 读入微博数据
	file, err := os.Open(*websitedata)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()
	reader := bufio.NewReader(file)

	count := 0
	line, err := reader.ReadString('\n')

	for ; err == nil; line, err = reader.ReadString('\n') {
		data := strings.Split(line, "||")
		count++
		if len(data) != 3 {
			continue
		}
		bm := BookMark{}
		bm.Id = uint64(time.Now().UnixNano())
		bm.Owner = data[0]
		bm.Title = data[1]
		bm.Text = data[2]
		bms[bm.Id] = bm
	}

	fmt.Println(count)

	log.Print("添加索引")
	for docId, bookmark := range bms {

		searcher.IndexDocument(docId, types.DocumentIndexData{
			Content: bookmark.Title + bookmark.Text,
			Labels:  []string{bookmark.Owner},
		})
	}

	searcher.FlushIndex()
	log.Printf("索引了%d条微博\n", len(bms))
}

/*******************************************************************************
    评分
*******************************************************************************/

/*******************************************************************************
    JSON-RPC
*******************************************************************************/
type JsonResponse struct {
	Docs []*BookMark `json:"docs"`
}

func JsonRpcServer(w http.ResponseWriter, req *http.Request) {
	query := req.URL.Query().Get("query")
	owner := req.URL.Query().Get("owner")
	output := searcher.Search(types.SearchRequest{
		Text:   query,
		Labels: []string{owner},
	})

	// 整理为输出格式
	docs := []*BookMark{}
	for _, doc := range output.Docs {
		bm := bms[doc.DocId]
		for _, t := range output.Tokens {
			bm.Text = strings.Replace(bm.Text, t, "<font color=red>"+t+"</font>", -1)
		}
		docs = append(docs, &bm)
	}
	response, _ := json.Marshal(&JsonResponse{Docs: docs})

	w.Header().Set("Content-Type", "application/json")
	io.WriteString(w, string(response))
}

/*******************************************************************************
	主函数
*******************************************************************************/
func main() {
	// 解析命令行参数
	flag.Parse()

	// 初始化
	// gob.Register(WeiboScoringFields{})
	log.Print("引擎开始初始化")
	searcher.Init(types.EngineInitOptions{
		SegmenterDictionaries: *dictFile,
		StopTokenFile:         *stopTokenFile,
		IndexerInitOptions: &types.IndexerInitOptions{
			IndexType: types.LocationsIndex,
		},
		// 如果你希望使用持久存储，启用下面的选项
		// 默认使用boltdb持久化，如果你希望修改数据库类型
		// 请修改 WUKONG_STORAGE_ENGINE 环境变量
		// UsePersistentStorage: true,
		// PersistentStorageFolder: "weibo_search",
	})
	log.Print("引擎初始化完毕")
	bms = make(map[uint64]BookMark)

	// 索引
	log.Print("建索引开始")
	go indexBookMark()
	log.Print("建索引完毕")

	// 捕获ctrl-c
	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt)
	go func() {
		for _ = range c {
			log.Print("捕获Ctrl-c，退出服务器")
			searcher.Close()
			os.Exit(0)
		}
	}()

	http.HandleFunc("/json", JsonRpcServer)
	http.Handle("/", http.FileServer(http.Dir(*staticFolder)))
	log.Print("服务器启动")
	log.Fatal(http.ListenAndServe(":8080", nil))
}
