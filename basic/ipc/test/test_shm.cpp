#include "../fixed_shm.h"
#include <gtest/gtest.h>

struct  person_t {
  char    name[20];
  int     age;
  double  salary;
};

class FixedShmTest :public ::testing::Test {
  protected:
    virtual void SetUp() {
      file_ = "./test.shm";
      size_ = 1 * 1024* 1024;
      shm = std::make_shared<detail::fixed_shm> (file_.c_str(), size_);
    }
    
    virtual void TearDown(){
      shm = nullptr;
      if(access(file_.c_str(), 0) == 0){
          remove(file_.c_str());
      }
    }

    std::string file_;
    uint64_t  size_;
    std::shared_ptr<detail::fixed_shm>  shm;
};

TEST_F(FixedShmTest,  WriteString) {
  //detail::fixed_shm shm(file_.c_str(), size_);
  EXPECT_NE(shm, nullptr);
  EXPECT_EQ(shm->open(), true);
  EXPECT_NE(shm->map(), nullptr);
  auto ptr = shm->mem(0);
  EXPECT_NE(ptr, nullptr);
  memcpy(ptr,  "hello world", 12);
  auto t = shm->mem(0);
  EXPECT_NE(t, nullptr);
  EXPECT_STREQ(ptr, t);
  
}



TEST_F(FixedShmTest, WriteToTest){
  EXPECT_NE(shm, nullptr);
  
  person_t xiaoming;
  strcpy(xiaoming.name, "xiaoming");
  xiaoming.age = 30;
  xiaoming.salary= 30000;
  
  EXPECT_EQ(shm->open(), true);
  EXPECT_NE(shm->map(), nullptr);
  EXPECT_EQ(shm->writeto(0, reinterpret_cast<char*>(&xiaoming), sizeof(xiaoming)), sizeof(xiaoming));

  person_t  pt;
  EXPECT_EQ(shm->readfrom(0, (char*)&pt, sizeof(pt)), (int)sizeof(pt));

  EXPECT_STREQ(xiaoming.name, pt.name);
  EXPECT_EQ(xiaoming.age, pt.age);
  EXPECT_DOUBLE_EQ(xiaoming.salary, pt.salary);
}
