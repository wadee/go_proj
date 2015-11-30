package main

import (
	"bufio"
	"fmt"
	"github.com/PuerkitoBio/goquery"
	"os"
	"strings"
)

func main() {
	f, err := os.Open("./bookmarks_15_11_25.html")
	if err != nil {
		panic("open failed")
	}
	defer f.Close()

	out_file := "formated_bookmark"
	of, err := os.Create(out_file)
	if err != nil {
		panic("output file create fail")
	}
	defer of.Close()

	fw := bufio.NewWriter(of)

	html_src := bufio.NewReader(f)

	query, err := goquery.NewDocumentFromReader(html_src)
	if err != nil {
		panic("go query error")
	}

	query.Find("DT A").Each(func(i int, s *goquery.Selection) {
		var content string
		href, isExist := s.Attr("href")
		if isExist != true {
			return
		}
		if is_http_prefix := strings.HasPrefix(href, "http"); is_http_prefix != true {
			return
		}
		content = "liangzehua" + "||||" + href + "||||" + s.Text() + "\n"
		fw.WriteString(content)
	})
	fmt.Println("done. output file : " + out_file)
}
