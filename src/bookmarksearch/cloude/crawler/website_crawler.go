//
package main

/*
Packages must be imported:
    "core/common/page"
    "core/spider"
Pckages may be imported:
    "core/pipeline": scawler result persistent;
    "github.com/PuerkitoBio/goquery": html dom parser.
*/
import (
	"bufio"
	"fmt"
	"github.com/PuerkitoBio/goquery"
	"github.com/hu17889/go_spider/core/common/page"
	"github.com/hu17889/go_spider/core/pipeline"
	"github.com/hu17889/go_spider/core/spider"
	"os"
	"regexp"
	"strings"
)

type MyPageProcesser struct {
}

func NewMyPageProcesser() *MyPageProcesser {
	return &MyPageProcesser{}
}

func removeDuplicate(slis *[]string) {
	found := make(map[string]bool)
	j := 0
	for i, val := range *slis {
		if _, ok := found[val]; !ok {
			found[val] = true
			(*slis)[j] = (*slis)[i]
			j++
		}
	}
	*slis = (*slis)[:j]
}

// Parse html dom here and record the parse result that we want to Page.
// Package goquery (http://godoc.org/github.com/PuerkitoBio/goquery) is used to parse html.
func (this *MyPageProcesser) Process(p *page.Page) {
	if !p.IsSucc() {
		println(p.Errormsg())
		return
	}
	var fetch_content string
	query := p.GetHtmlParser()
	content := p.GetBodyStr()
	reg := regexp.MustCompile(`class="([0-9a-zA-Z_-]*content[0-9a-zA-Z_-]*)"`)
	reg_res := reg.FindAllStringSubmatch(content, -1)
	class_content := make([]string, 0)
	for _, class := range reg_res {
		submatch := class[1]
		class_content = append(class_content, submatch)
	}
	removeDuplicate(&class_content)

	for _, class := range class_content {

		query.Find("." + class).Each(func(i int, s *goquery.Selection) {
			text := strings.Trim(s.Text(), " \t\n")
			text = strings.Replace(text, " ", "", -1)
			text = strings.Replace(text, "\n", "", -1)
			text = strings.Replace(text, "\t", "", -1)

			if text != "" {
				fetch_content = fetch_content + text
			}
		})
	}

	p.AddField("content", fetch_content)

}

func (this *MyPageProcesser) Finish() {
	fmt.Printf("TODO:before end spider \r\n")
}

func main() {
	// Spider input:
	//  PageProcesser ;
	//  Task name used in Pipeline for record;
	f, err := os.Open("./formated_bookmark")
	if err != nil {
		panic("f open error")
	}
	defer f.Close()

	sp := spider.NewSpider(NewMyPageProcesser(), "BookMarkSearch")

	br := bufio.NewReader(f)

	urls := make(map[string]string)
	line, err := br.ReadString('\n')
	for ; err == nil; line, err = br.ReadString('\n') {
		data := strings.Split(line, "||")
		url := data[1]
		urltag := data[0] + "||" + data[2]
		urls[url] = urltag
	}

	sp.AddUrls(urls, "html")

	// sp.AddPipeline(pipeline.NewPipelineConsole()).
	sp.AddPipeline(pipeline.NewPipelineFile("./crawler_result.dat")).
		SetThreadnum(3). // Crawl request by three Coroutines
		Run()
}
