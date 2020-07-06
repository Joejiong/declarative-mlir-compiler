#include "impl.h"
#include "rx-cpp/src/lua-str.h"

#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>

extern "C" void print_one(TObject *);

namespace lua {
namespace {

void formatNoop(std::ostream &) {}

void formatPrint(std::ostream &os) {
  os << std::left << std::setw(8);
}

template <typename FormatFcn>
void print_impl(FormatFcn formatOutput, TPack *pack) {
  while (pack->idx != pack->size) {
    auto *val = lua_pack_pull_one(pack);
    switch (lua_get_type(val)) {
    case NIL:
      // ignore last nil
      if (pack->idx != pack->size) {
        formatOutput(std::cout);
        std::cout << "nil";
      }
      break;
    case BOOL:
      formatOutput(std::cout);
      if (lua_get_bool_val(val)) {
        std::cout << "true";
      } else {
        std::cout << "false";
      }
      break;
    case NUM:
      formatOutput(std::cout);
      if (lua_is_int(val)) {
        std::cout << lua_get_int64_val(val);
      } else {
        std::cout << lua_get_double_val(val);
      }
      break;
    case STR:
      formatOutput(std::cout);
      std::cout << lua::as_std_string(val);
      break;
    case TBL:
      std::cout << "table: ";
      formatOutput(std::cout);
      std::cout << std::hex << val->gc->ptable;
      break;
    case FCN:
      std::cout << "function: 0x";
      for (unsigned i = 0; i < sizeof(lua_fcn_t); ++i) {
        if (i == sizeof(lua_fcn_t) - 1) {
          formatOutput(std::cout);
        }
        std::cout << std::hex << (int)((unsigned char *) &val->gc->fcn_addr)[i];
      }
      break;
    }
  }
}

TPack *fcn_builtin_print(TPack *, TPack *pack) {
  print_impl(&formatPrint, pack);
  std::cout << std::endl;

  auto *ret = lua_new_pack(1);
  auto *nil = lua_alloc();
  lua_set_type(nil, NIL);
  lua_pack_push(ret, nil);
  return ret;
}

int64_t correct_str_offset(std::string &textStr, int64_t offset) {
  if (offset > 0) {
    --offset;
    if (offset >= textStr.size()) {
      offset = textStr.size();
    }
  } else if (offset < 0) {
    offset = (int64_t) textStr.size() + offset;
    if (offset < 0) {
      offset = 0;
    }
  }
  return offset;
}

TPack *fcn_builtin_string_find(TPack *, TPack *pack) {
  auto *text = lua_pack_pull_one(pack);
  auto *pattern = lua_pack_pull_one(pack);
  auto *pos = lua_pack_pull_one(pack);
  auto &textStr = as_std_string(text);
  auto &patStr = as_std_string(pattern);
  int64_t offset = 0;
  if (lua_get_type(pos) != NIL) {
    offset = correct_str_offset(textStr, lua_get_int64_val(pos));
  }
  LuaMatch m;
  auto n = str_match(textStr.c_str() + offset, textStr.size() - offset,
                     patStr.c_str(), &m);
  if (n == 0) {
    auto *ret = lua_new_pack(1);
    auto *nil = lua_alloc();
    lua_set_type(nil, NIL);
    lua_pack_push(ret, nil);
    return ret;
  }
  auto *ret = lua_new_pack(2);
  auto *start = lua_alloc();
  lua_set_type(start, NUM);
  lua_set_int64_val(start, m.start + 1 + offset);
  lua_pack_push(ret, start);
  auto *end = lua_alloc();
  lua_set_type(end, NUM);
  lua_set_int64_val(end, m.end + offset);
  lua_pack_push(ret, end);
  return ret;
}

TPack *fcn_builtin_string_sub(TPack *, TPack *pack) {
  auto *text = lua_pack_pull_one(pack);
  auto *start = lua_pack_pull_one(pack);
  auto *end = lua_pack_pull_one(pack);

  auto &textStr = as_std_string(text);
  auto istart = correct_str_offset(textStr, lua_get_int64_val(start));
  auto iend = correct_str_offset(textStr, lua_get_int64_val(end));

  auto *substring = lua_alloc();
  lua_set_type(substring, STR);
  lua_alloc_gc(substring);
  substring->gc->pstring = new std::string{textStr.substr(istart, iend - istart + 1)};

  auto *ret = lua_new_pack(1);
  lua_pack_push(ret, substring);
  return ret;
}

TPack *fcn_builtin_table_insert(TPack *, TPack *pack) {
  auto *tbl = lua_pack_pull_one(pack);
  auto *val = lua_pack_pull_one(pack);
  auto *listSz = lua_list_size(tbl);

  auto *one = lua_alloc();
  lua_set_type(one, NUM);
  lua_set_int64_val(one, 1);
  auto *nextIndex = lua_add(listSz, one);
  lua_table_set(tbl, nextIndex, val);

  auto *ret = lua_new_pack(1);
  auto *nil = lua_alloc();
  lua_set_type(nil, NIL);
  lua_pack_push(ret, nil);
  return ret;
}

TPack *fcn_builtin_io_read(TPack *, TPack *pack) {
  auto *ctrl = lua_pack_pull_one(pack);

  auto *ret = lua_alloc();

  if (std::cin.peek() == EOF) {
    lua_set_type(ret, NIL);
  } else if (lua_get_type(ctrl) == NUM) {
    lua_set_type(ret, STR);
    lua_alloc_gc(ret);
    auto *pstring = new std::string{};
    ret->gc->pstring = pstring;

    int numChars;
    if (lua_is_int(ctrl)) {
      numChars = lua_get_int64_val(ctrl);
    } else {
      numChars = (int) lua_get_double_val(ctrl);
    }
    for (int i = 0; i < numChars; ++i) {
      auto nextChar = std::cin.get();
      if (nextChar == EOF) {
        break;
      }
      *pstring += nextChar;
    }
  } else if (lua_get_type(ctrl) == STR) {
    if (lua::as_std_string(ctrl) == "*all") {
      lua_set_type(ret, STR);
      lua_alloc_gc(ret);
      auto *pstring = new std::string{};
      ret->gc->pstring = pstring;

      int nextChar;
      while ((nextChar = std::cin.get()) != EOF) {
        *pstring += nextChar;
      }
    } else if (lua::as_std_string(ctrl) == "*number") {
      double number;
      std::cin >> number;

      lua_set_type(ret, NUM);
      if (std::ceilf(number) == number) {
        lua_set_int64_val(ret, (int64_t) number);
      } else {
        lua_set_double_val(ret, number);
      }
    } else {
      goto read_line;
    }
  } else {
read_line:
    lua_set_type(ret, STR);
    lua_alloc_gc(ret);
    auto *pstring = new std::string{};
    ret->gc->pstring = pstring;
    std::getline(std::cin, *pstring);
  }

  auto *retpack = lua_new_pack(1);
  lua_pack_push(retpack, ret);
  return retpack;
}

TPack *fcn_builtin_io_write(TPack *, TPack *pack) {
  print_impl(&formatNoop, pack);

  auto *ret = lua_new_pack(1);
  auto *nil = lua_alloc();
  lua_set_type(nil, NIL);
  lua_pack_push(ret, nil);
  return ret;
}

TPack *fcn_builtin_math_random(TPack *, TPack *pack) {
  thread_local std::random_device rd;
  thread_local std::default_random_engine e2{rd()};

  auto *r = lua_alloc();
  lua_set_type(r, NUM);

  if (lua_pack_get_size(pack) == 0) {
    std::uniform_real_distribution<double> dist{0, 1};
    lua_set_double_val(r, dist(e2));
  } else if (lua_pack_get_size(pack) == 1) {
    auto *upper = lua_pack_pull_one(pack);
    std::uniform_int_distribution<int64_t> dist{1, lua_get_int64_val(upper)};
    lua_set_int64_val(r, dist(e2));
  } else {
    auto *lower = lua_pack_pull_one(pack);
    auto *upper = lua_pack_pull_one(pack);
    std::uniform_int_distribution<int64_t> dist{lua_get_int64_val(lower),
                                                lua_get_int64_val(upper)};
    lua_set_int64_val(r, dist(e2));
  }

  auto *ret = lua_new_pack(1);
  lua_pack_push(ret, r);
  return ret;
}

TObject *construct_builtin_print(void) {
  TObject *print = lua_alloc();
  lua_set_type(print, FCN);
  lua_alloc_gc(print);
  lua_set_fcn_addr(print, &fcn_builtin_print);
  lua_set_capture_pack(print, nullptr);
  return print;
}

TObject *construct_builtin_string(void) {
  TObject *string = lua_alloc();
  lua_set_type(string, TBL);
  lua_alloc_gc(string);
  lua_init_table(string);
  {
    TObject *find = lua_alloc();
    lua_set_type(find, FCN);
    lua_alloc_gc(find);
    lua_set_fcn_addr(find, &fcn_builtin_string_find);
    lua_set_capture_pack(find, nullptr);
    auto *key = lua_load_string("find", 4);
    lua_table_set(string, key, find);
  }
  {
    TObject *sub = lua_alloc();
    lua_set_type(sub, FCN);
    lua_alloc_gc(sub);
    lua_set_fcn_addr(sub, &fcn_builtin_string_sub);
    lua_set_capture_pack(sub, nullptr);
    auto *key = lua_load_string("sub", 3);
    lua_table_set(string, key, sub);
  }
  return string;
}

TObject *construct_builtin_table(void) {
  TObject *table = lua_alloc();
  lua_set_type(table, TBL);
  lua_alloc_gc(table);
  lua_init_table(table);
  {
    TObject *insert = lua_alloc();
    lua_set_type(insert, FCN);
    lua_alloc_gc(insert);
    lua_set_fcn_addr(insert, &fcn_builtin_table_insert);
    lua_set_capture_pack(insert, nullptr);
    auto *key = lua_load_string("insert", 6);
    lua_table_set(table, key, insert);
  }
  return table;
}

TObject *construct_builtin_io(void) {
  TObject *io = lua_alloc();
  lua_set_type(io, TBL);
  lua_alloc_gc(io);
  lua_init_table(io);
  {
    TObject *read = lua_alloc();
    lua_set_type(read, FCN);
    lua_alloc_gc(read);
    lua_set_fcn_addr(read, &fcn_builtin_io_read);
    lua_set_capture_pack(read, nullptr);
    auto *key = lua_load_string("read", 4);
    lua_table_set(io, key, read);
  }
  {
    TObject *write = lua_alloc();
    lua_set_type(write, FCN);
    lua_alloc_gc(write);
    lua_set_fcn_addr(write, &fcn_builtin_io_write);
    lua_set_capture_pack(write, nullptr);
    auto *key = lua_load_string("write", 5);
    lua_table_set(io, key, write);
  }
  return io;
}

TObject *construct_builtin_math(void) {
  TObject *math = lua_alloc();
  lua_set_type(math, TBL);
  lua_alloc_gc(math);
  lua_init_table(math);
  {
    TObject *random = lua_alloc();
    lua_set_type(random, FCN);
    lua_alloc_gc(random);
    lua_set_fcn_addr(random, &fcn_builtin_math_random);
    lua_set_capture_pack(random, nullptr);
    auto *key = lua_load_string("random", 6);
    lua_table_set(math, key, random);
  }
  return math;
}

} // end anonymous namespace
} // end namespace lua

extern "C" {

TObject *builtin_print = lua::construct_builtin_print();
TObject *builtin_string = lua::construct_builtin_string();
TObject *builtin_table = lua::construct_builtin_table();
TObject *builtin_io = lua::construct_builtin_io();
TObject *builtin_random = lua::construct_builtin_math();
TObject *builtin_math = lua::construct_builtin_math();

// special debugging function
void print_one(TObject *val) {
  auto *pack = lua_new_pack(1);
  lua_pack_push(pack, val);
  lua::print_impl(lua::formatNoop, pack);
  std::cout << std::endl;
  lua_delete_pack(pack);
}

}