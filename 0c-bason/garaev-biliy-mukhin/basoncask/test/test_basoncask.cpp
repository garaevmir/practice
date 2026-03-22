#include "../src/basoncask.hpp"
#include "../src/bason.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <random>

void test_bason_codec() {
  auto enc = bason::encode(bason::str("key", "value"));
  assert(enc.size() > 0);
  auto [dec, n] = bason::decode(enc.data(), enc.size());
  assert(dec.key == "key");
  assert(dec.value == "value");
  assert(dec.type == bason::BasonType::String);
}

void test_basoncask_basic() {
  std::string dir = "/tmp/basoncask_test_12345";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  {
    auto db = BasonCask::open({.dir = dir, .sync_on_write = true});
    db->put("hello", bason::str("", "world"));
    db->put("num", bason::BasonRecord{.type = bason::BasonType::Number, .key = "", .value = "42", .children = {}});
    db->close();
  }

  {
    auto db2 = BasonCask::open({.dir = dir});
    auto v = db2->get("hello");
    assert(v && v->value == "world");
    auto n = db2->get("num");
    assert(n && n->value == "42");
    db2->close();
  }

  {
    auto db3 = BasonCask::open({.dir = dir});
    db3->del("hello");
    db3->close();
  }

  {
    auto db4 = BasonCask::open({.dir = dir});
    assert(!db4->get("hello"));
    assert(db4->get("num")->value == "42");
    db4->close();
  }

  std::filesystem::remove_all(dir);
  std::cout << "BasonCask basic OK\n";
}

void test_basoncask_recovery() {
  std::string dir = "/tmp/basoncask_recovery_" + std::to_string(std::random_device{}());
  std::filesystem::create_directories(dir);

  {
    auto db = BasonCask::open({.dir = dir, .sync_on_write = true});
    db->put("a", bason::str("", "1"));
    db->put("b", bason::str("", "2"));
    db->put("c", bason::str("", "3"));
    db->close();
  }

  {
    auto db = BasonCask::open({.dir = dir});
    assert(db->get("a")->value == "1");
    assert(db->get("b")->value == "2");
    assert(db->get("c")->value == "3");
    db->close();
  }

  std::filesystem::remove_all(dir);
  std::cout << "BasonCask recovery OK\n";
}

int main() {
  test_bason_codec();
  test_basoncask_basic();
  test_basoncask_recovery();
  std::cout << "All tests passed\n";
  return 0;
}
