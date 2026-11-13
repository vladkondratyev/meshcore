#ifdef BOOK_READER

#include "BookStore.h"
#include <helpers/BaseSerialInterface.h>   // MAX_FRAME_SIZE

#ifndef BOOKFS_ADDR
  #error "BOOK_READER needs BOOKFS_ADDR and BOOKFS_SIZE"
#endif
#define BOOKFS_BLOCK      512
#define BOOK_RESERVE      (8 * BOOKFS_BLOCK)   // room for library rewrites
#define BOOK_LIB_FILE     "/lib"
#define BOOK_TMP_FILE     "/upload"
#define BOOK_LIB_MAGIC    0x314B4F42UL           // "BOK1"
#define BOOK_CHUNK_MAX    (MAX_FRAME_SIZE - 6)

#define CMD_BOOK          0xB0
#define BOOK_SUB_INFO     1
#define BOOK_SUB_LIST     2
#define BOOK_SUB_BEGIN    3
#define BOOK_SUB_DATA     4
#define BOOK_SUB_END      5
#define BOOK_SUB_DELETE   6
#define BOOK_SUB_ABORT    7
#define BOOK_PROTO_VER    1

BookStore book_store;

static uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}

static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

static int countBlock(void* p, lfs_block_t) {
  (*(lfs_size_t*)p)++;
  return 0;
}

BookStore::BookStore() : _fs(BOOKFS_ADDR, BOOKFS_SIZE, BOOKFS_BLOCK), _up_file(_fs) {}

void BookStore::bookPath(char* buf, uint8_t id) {
  sprintf(buf, "/b%u", (unsigned)id);
}

bool BookStore::begin() {
  _ready = _fs.begin();          // formats the region when it does not mount
  if (!_ready) return false;
  _fs.remove(BOOK_TMP_FILE);     // left over from an upload cut short
  loadLibrary();
  return true;
}

bool BookStore::loadLibrary() {
  _count = 0;
  _current = -1;
  File f = _fs.open(BOOK_LIB_FILE, FILE_O_READ);
  if (!f) return false;
  uint32_t magic = 0;
  uint8_t hdr[4] = {0};
  bool ok = f.read((uint8_t*)&magic, 4) == 4 && magic == BOOK_LIB_MAGIC && f.read(hdr, 4) == 4;
  if (ok) {
    uint8_t n = hdr[0] > BOOK_MAX ? BOOK_MAX : hdr[0];
    for (uint8_t i = 0; i < n; i++) {
      if (f.read((uint8_t*)&_lib[_count], sizeof(BookEntry)) != sizeof(BookEntry)) break;
      BookEntry& e = _lib[_count];
      e.title[BOOK_TITLE_MAX - 1] = 0;
      char path[12];
      bookPath(path, e.id);
      if (_fs.exists(path)) _count++;   // drop entries whose text is gone
    }
    _current = hdr[1] == 0xFF ? -1 : hdr[1];
    if (_current >= 0 && indexOf((uint8_t)_current) < 0) _current = -1;
  }
  f.close();
  return ok;
}

bool BookStore::saveLibrary() {
  if (!_ready) return false;
  _fs.remove(BOOK_LIB_FILE);
  File f = _fs.open(BOOK_LIB_FILE, FILE_O_WRITE);
  if (!f) return false;
  uint32_t magic = BOOK_LIB_MAGIC;
  uint8_t hdr[4] = { _count, (uint8_t)(_current < 0 ? 0xFF : _current), 0, 0 };
  f.write((const uint8_t*)&magic, 4);
  f.write(hdr, 4);
  for (uint8_t i = 0; i < _count; i++) f.write((const uint8_t*)&_lib[i], sizeof(BookEntry));
  f.close();
  return true;
}

int BookStore::indexOf(uint8_t id) const {
  for (uint8_t i = 0; i < _count; i++) if (_lib[i].id == id) return i;
  return -1;
}

uint8_t BookStore::freeId() const {
  for (uint16_t id = 0; id < 255; id++) {
    if (indexOf((uint8_t)id) < 0) return (uint8_t)id;
  }
  return 0xFF;
}

void BookStore::setCurrent(int16_t id) {
  if (id >= 0 && indexOf((uint8_t)id) < 0) id = -1;
  if (id == _current) return;
  _current = id;
  saveLibrary();
}

int BookStore::read(uint8_t id, uint32_t pos, uint8_t* dst, int n) {
  if (!_ready) return 0;
  char path[12];
  bookPath(path, id);
  File f = _fs.open(path, FILE_O_READ);
  if (!f) return 0;
  int got = 0;
  if (f.seek(pos)) got = f.read(dst, n);
  f.close();
  return got < 0 ? 0 : got;
}

bool BookStore::setBookmark(uint8_t id, uint32_t pos, uint8_t face, uint8_t flags) {
  int i = indexOf(id);
  if (i < 0) return false;
  _lib[i].pos = pos;
  _lib[i].face = face;
  _lib[i].flags = flags;
  return true;
}

uint8_t BookStore::remove(uint8_t id) {
  int i = indexOf(id);
  if (i < 0) return BOOK_ERR_NOT_FOUND;
  char path[12];
  bookPath(path, id);
  _fs.remove(path);
  for (int k = i; k + 1 < _count; k++) _lib[k] = _lib[k + 1];
  _count--;
  if (_current == id) _current = -1;
  _gen++;
  saveLibrary();
  return BOOK_OK;
}

uint32_t BookStore::totalBytes() const {
  return BOOKFS_SIZE;
}

uint32_t BookStore::freeBytes() {
  if (!_ready) return 0;
  lfs_size_t used = 0;
  if (lfs_traverse(_fs._getFS(), countBlock, &used) != 0) return 0;
  uint32_t total = BOOKFS_SIZE / BOOKFS_BLOCK;
  if (used >= total) return 0;
  uint32_t free_b = (total - used) * BOOKFS_BLOCK;
  return free_b > BOOK_RESERVE ? free_b - BOOK_RESERVE : 0;
}

// LittleFS v1 keeps a skip list in every block of a file; leave room for it.
static uint32_t storedSize(uint32_t size) {
  return size + size / 32 + 2 * BOOKFS_BLOCK;
}

uint8_t BookStore::uploadBegin(uint32_t size, uint32_t crc, const char* title, uint8_t title_len, uint8_t& id_out) {
  uploadAbort();
  if (!_ready) return BOOK_ERR_FS;
  if (size == 0) return BOOK_ERR_ARGS;
  if (_count >= BOOK_MAX) return BOOK_ERR_FULL;
  if (storedSize(size) > freeBytes()) return BOOK_ERR_SPACE;
  _up_file = _fs.open(BOOK_TMP_FILE, FILE_O_WRITE);
  if (!_up_file) return BOOK_ERR_FS;
  if (title_len >= BOOK_TITLE_MAX) title_len = BOOK_TITLE_MAX - 1;
  memcpy(_up_title, title, title_len);
  _up_title[title_len] = 0;
  _up_size = size;
  _up_crc = crc;
  _up_off = 0;
  _up_run = 0;
  _up = true;
  id_out = freeId();
  return BOOK_OK;
}

uint8_t BookStore::uploadData(uint32_t offset, const uint8_t* data, size_t len, uint32_t& next_out) {
  next_out = _up_off;
  if (!_up) return BOOK_ERR_NO_UPLOAD;
  if (offset != _up_off) return BOOK_ERR_OFFSET;   // the client resends from next_out
  if (len == 0 || _up_off + len > _up_size) return BOOK_ERR_ARGS;
  if (_up_file.write(data, len) != len) {
    uploadAbort();
    return BOOK_ERR_FS;
  }
  _up_run = crc32Update(_up_run, data, len);
  _up_off += len;
  next_out = _up_off;
  return BOOK_OK;
}

uint8_t BookStore::uploadEnd(uint8_t& id_out) {
  if (!_up) return BOOK_ERR_NO_UPLOAD;
  _up_file.close();
  _up = false;
  if (_up_off != _up_size || _up_run != _up_crc) {
    _fs.remove(BOOK_TMP_FILE);
    return _up_off != _up_size ? BOOK_ERR_ARGS : BOOK_ERR_CRC;
  }
  uint8_t id = freeId();
  char path[12];
  bookPath(path, id);
  _fs.remove(path);
  if (_count >= BOOK_MAX || id == 0xFF || !_fs.rename(BOOK_TMP_FILE, path)) {
    _fs.remove(BOOK_TMP_FILE);
    return BOOK_ERR_FS;
  }
  BookEntry& e = _lib[_count++];
  memset(&e, 0, sizeof(e));
  e.id = id;
  e.face = 1;
  e.size = _up_size;
  e.crc = _up_crc;
  memcpy(e.title, _up_title, BOOK_TITLE_MAX);
  _gen++;
  saveLibrary();
  id_out = id;
  return BOOK_OK;
}

void BookStore::uploadAbort() {
  if (_up) {
    _up_file.close();
    _up = false;
  }
  if (_ready) _fs.remove(BOOK_TMP_FILE);
}

size_t BookStore::handleCommand(const uint8_t* in, size_t len, uint8_t* out) {
  uint8_t sub = len >= 2 ? in[1] : 0;
  out[0] = CMD_BOOK;
  out[1] = sub;
  out[2] = BOOK_OK;
  size_t n = 3;
  switch (sub) {
    case BOOK_SUB_INFO:
      out[n++] = BOOK_PROTO_VER;
      out[n++] = _count;
      out[n++] = BOOK_MAX;
      put32(&out[n], freeBytes()); n += 4;
      put32(&out[n], totalBytes()); n += 4;
      out[n++] = _current < 0 ? 0xFF : (uint8_t)_current;
      put32(&out[n], BOOK_CHUNK_MAX); n += 4;
      break;
    case BOOK_SUB_LIST: {
      uint8_t idx = len >= 3 ? in[2] : 0;
      const BookEntry* e = entry(idx);
      if (!e) { out[2] = BOOK_ERR_NOT_FOUND; break; }
      out[n++] = idx;
      out[n++] = _count;
      out[n++] = e->id;
      out[n++] = e->face;
      put32(&out[n], e->size); n += 4;
      put32(&out[n], e->pos); n += 4;
      size_t tl = strlen(e->title);
      memcpy(&out[n], e->title, tl); n += tl;
      break;
    }
    case BOOK_SUB_BEGIN: {
      if (len < 10) { out[2] = BOOK_ERR_ARGS; break; }
      uint8_t id = 0xFF;
      out[2] = uploadBegin(get32(&in[2]), get32(&in[6]), (const char*)&in[10], (uint8_t)(len - 10), id);
      out[n++] = id;
      put32(&out[n], BOOK_CHUNK_MAX); n += 4;
      break;
    }
    case BOOK_SUB_DATA: {
      uint32_t next = _up_off;
      if (len < 7) out[2] = BOOK_ERR_ARGS;
      else out[2] = uploadData(get32(&in[2]), &in[6], len - 6, next);
      put32(&out[n], next); n += 4;
      break;
    }
    case BOOK_SUB_END: {
      uint8_t id = 0xFF;
      out[2] = uploadEnd(id);
      out[n++] = id;
      break;
    }
    case BOOK_SUB_DELETE:
      out[2] = len >= 3 ? remove(in[2]) : BOOK_ERR_ARGS;
      break;
    case BOOK_SUB_ABORT:
      uploadAbort();
      break;
    default:
      out[2] = BOOK_ERR_ARGS;
      break;
  }
  return n;
}

#endif
