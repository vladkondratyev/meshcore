#pragma once

// E-book library on its own LittleFS region (BOOKFS_ADDR, BOOKFS_SIZE).
// Books are Windows-1251 text, uploaded over the companion link (CMD_BOOK)
// by tools/book_uploader. The library file keeps titles, sizes and bookmarks.

#ifdef BOOK_READER

#include <Arduino.h>
#include <CustomLFS.h>
#include <helpers/reader/ReaderLayout.h>

#ifndef BOOK_MAX
  #define BOOK_MAX 16
#endif
#define BOOK_TITLE_MAX 48     // bytes of 1251, with the terminating zero

struct BookEntry {
  uint8_t id;
  uint8_t face;               // font size index
  uint8_t flags;              // bit 0: justify
  uint8_t reserved;
  uint32_t size;
  uint32_t crc;
  uint32_t pos;               // bookmark, byte offset
  char title[BOOK_TITLE_MAX]; // 1251
};

enum BookStatus : uint8_t {
  BOOK_OK = 0,
  BOOK_ERR_SPACE = 1,
  BOOK_ERR_FULL = 2,
  BOOK_ERR_FS = 3,
  BOOK_ERR_CRC = 4,
  BOOK_ERR_OFFSET = 5,
  BOOK_ERR_NOT_FOUND = 6,
  BOOK_ERR_NO_UPLOAD = 7,
  BOOK_ERR_ARGS = 8,
};

class BookStore {
  CustomLFS _fs;
  BookEntry _lib[BOOK_MAX];
  uint8_t _count = 0;
  int16_t _current = -1;      // id of the open book
  uint16_t _gen = 0;          // bumps when books are added or removed
  bool _ready = false;

  // upload in progress
  bool _up = false;
  File _up_file;
  uint32_t _up_size = 0, _up_crc = 0, _up_off = 0, _up_run = 0;
  char _up_title[BOOK_TITLE_MAX];

  bool loadLibrary();
  bool saveLibrary();
  int indexOf(uint8_t id) const;
  uint8_t freeId() const;
  static void bookPath(char* buf, uint8_t id);

public:
  BookStore();
  bool begin();
  bool ready() const { return _ready; }
  uint16_t generation() const { return _gen; }

  uint8_t count() const { return _count; }
  const BookEntry* entry(int idx) const { return idx >= 0 && idx < _count ? &_lib[idx] : nullptr; }
  const BookEntry* find(uint8_t id) const { int i = indexOf(id); return i >= 0 ? &_lib[i] : nullptr; }
  int16_t current() const { return _current; }
  void setCurrent(int16_t id);

  int read(uint8_t id, uint32_t pos, uint8_t* dst, int n);
  bool setBookmark(uint8_t id, uint32_t pos, uint8_t face, uint8_t flags);   // in RAM only
  bool flush() { return saveLibrary(); }
  uint8_t remove(uint8_t id);

  uint32_t totalBytes() const;
  uint32_t freeBytes();

  uint8_t uploadBegin(uint32_t size, uint32_t crc, const char* title, uint8_t title_len, uint8_t& id_out);
  uint8_t uploadData(uint32_t offset, const uint8_t* data, size_t len, uint32_t& next_out);
  uint8_t uploadEnd(uint8_t& id_out);
  void uploadAbort();
  bool uploading() const { return _up; }

  // CMD_BOOK frame from the companion link; writes the reply, returns its length.
  size_t handleCommand(const uint8_t* in, size_t len, uint8_t* out);
};

// ReaderText over one stored book.
class BookText : public ReaderText {
  BookStore* _store = nullptr;
  uint8_t _id = 0;
  uint32_t _size = 0;
public:
  void open(BookStore* store, uint8_t id) {
    _store = store; _id = id;
    const BookEntry* e = store ? store->find(id) : nullptr;
    _size = e ? e->size : 0;
  }
  uint8_t id() const { return _id; }
  uint32_t size() override { return _size; }
  int read(uint32_t pos, uint8_t* dst, int n) override { return _store ? _store->read(_id, pos, dst, n) : 0; }
};

extern BookStore book_store;

#endif
