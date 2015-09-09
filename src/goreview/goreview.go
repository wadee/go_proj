package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/widuu/gojson"
)

type Annot struct {
	Object_id string `json:"object_id,omitempty"`
}

type INFO struct {
	Annotations []Annot `json:"annotations,omitempty"`
}

type OBJINFO struct {
	Urls []INFO `json:"urls,omitempty"`
}

// type DATA map[string][]map[string][]map[string]string

func main() {

	//读取文件
	f, err := os.Open("/tmp/zehua.out")
	if err != nil {
		panic("file open failed!")
	}
	defer f.Close()

	b := bufio.NewReader(f)
	line, err := b.ReadString('\n')

	for ; err == nil; line, err = b.ReadString('\n') {
		slice := strings.Split(line, "\t")
		mid := slice[0]
		uid := slice[1]
		content := slice[2]
		reg, err := regexp.Compile(`http:\/\/t.cn\/[a-zA-Z0-9]+`)

		var short_url string
		if err == nil {
			short_url = reg.FindString(content)

		}
		// 获取短链的对象信息
		// var obj_info OBJINFO
		// obj_info := make(map[string][]map[string][]map[string]string)

		post_data := map[string]string{"source": "3439264077", "url_short": short_url}

		ret_byte := Request("http://i.api.weibo.com/2/short_url/info.json", "GET", post_data)

		json_data := gojson.Json(string(ret_byte))

		var object_id string
		var score string
		var timestamp string
		if ann := json_data.Get("urls").Getindex(1).Get("annotations"); ann != nil {
			if ann_1 := ann.Getindex(1); ann_1 != nil {
				object_id = gojson.Json(string(ret_byte)).Get("urls").Getindex(1).Get("annotations").Getindex(1).Get("object_id").Tostring()
				// fmt.Println(gojson.Json(string(ret_byte)).Get("urls").Getindex(1).Get("annotations").Getindex(1).Get("dynamic").Get("score"), score)

				// if dynamic := temp.Get("dynamic"); dynamic != nil {
				// 	fmt.Println(temp)
				// 	score = dynamic.Get("score").Tostring()
				// }

			}

		}

		r, _ := regexp.Compile(`★+`)

		substr := r.FindString(content)

		score = strconv.Itoa(len(substr) / 3 * 2)

		mid_post_data := map[string]string{"source": "2346240190", "ids": mid}

		mid_ret := Request("http://i.api.weibo.com/2/statuses/show_batch.json", "GET", mid_post_data)

		json_data = gojson.Json(string(mid_ret))

		if status := json_data.Get("statuses").Getindex(1); status != nil {
			timestamp = status.Get("created_at").Tostring()
			t, err := time.Parse(time.RubyDate, timestamp)
			if err == nil {
				timestamp = strconv.FormatInt(t.Unix(), 10)
			}
		}

		review := map[string]string{"object_id": object_id, "score": score, "user_id": uid, "mid": mid, "last_modified": timestamp}
		json_review, err := json.Marshal(review)

		spr := "http://weibo.com/u/" + uid
		data := map[string]string{"source": "2346240190", "object_id": object_id, "user_id": uid, "review": string(json_review), "spr": spr}

		review_add := Request("http://i.api.weibo.com/2/darwin/review/add.json", "POST", data)
		fmt.Println(string(review_add))

		// fmt.Println(json_data.Get("statuses").Getindex(1).Get("created_at"))
		// json.Unmarshal(ret_byte, &obj_info)

		// var oid string
		// if value, ok := obj_info["urls"][0]["annotations"].([]interface{}); ok {
		// 	for _, item := range value {
		// 		if val, test := item.(map[string]interface{}); test {
		// 			if object_id, is_string := val["object_id"].(string); is_string {
		// 				oid = object_id

		// 			}
		// 			if dynaimc,
		// 		}
		// 	}
		// }
		time.Sleep(time.Second)
	}

	if err == io.EOF {
		fmt.Println(line)
	} else {
		fmt.Errorf("read err")
	}

	// ret := Request("http://www.baidu.com", "POST", nil)

}

func Request(api string, method string, post_data map[string]string) []byte {
	client := &http.Client{}

	data := url.Values{}
	for _k, _v := range post_data {
		data.Set(_k, _v)
	}

	_data := data.Encode()

	var request *http.Request
	var err error

	switch method {
	case "POST":
		fmt.Println(data)
		resp, err := http.PostForm(api, data)
		defer resp.Body.Close()
		if err == nil {

			body, err := ioutil.ReadAll(resp.Body)
			if err == nil {
				return body
			}
		}

	case "GET":
		actual_url := api + "?" + _data
		request, err = http.NewRequest(method, actual_url, nil)
	default:
		request, err = http.NewRequest(method, api, strings.NewReader(_data))
	}

	if err != nil {
		panic("new http request err")
	}
	resp, err := client.Do(request)
	body, err := ioutil.ReadAll(resp.Body)

	if err != nil {
		panic("response err!")
	}
	return body
}
