package models

import (
	"errors"
	"strconv"
	"time"
)

var (
	BookmarkCollection map[string]*Bookmark
)

func init() {
	//do some init thing
}

type Bookmark struct {
	DocId string `json: "docid"`
	Owner string `json: "owner"`
	Title string `json: "title"`
	Url   string `json: "url"`
}

func AddBookmark(bm Bookmark) string {
	bm.DocId = "bookmark_" + strconv.FormatInt(time.Now().UnixNano(), 10)
	BookmarkCollection[bm.DocId] = &bm
	return bm.DocId
}

func GetBookmark(DocId string) (bm *Bookmark, err error) {
	if bm, ok := BookmarkCollection[DocId]; ok {
		return bm, nil
	}
	return nil, errors.New("Bookmark not exists")
}

func GetAllBookmarks() map[string]*Bookmark {
	return BookmarkCollection
}

func DeleteBookmark(DocId string) {
	delete(BookmarkCollection, DocId)
}
