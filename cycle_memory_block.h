#ifndef _CYCLE_MEMORY_BLOCK_H_
#define _CYCLE_MEMORY_BLOCK_H_

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

class cycle_memory_block {
public:
	explicit cycle_memory_block(size_t  cap_);
	~cycle_memory_block() { if (_begin) free(_begin); }
public:
	const char*					begin() { return _begin; }
	const char*					end() { return _begin + _cap; }
	bool						empty() { return _data_start == _data_end; }
	char*						data() { return _data_start; }
	size_t						cap() { return _cap; }
	inline bool					append(void* data, size_t len);
	inline void					pop(size_t size_);//
	inline size_t				c_length();// { }
	size_t						size() { if (_data_end < _data_start) return _data_end + _cap - _data_start; return _data_end - _data_start; }
	void						clear() { _data_end = _data_start = _begin; }
private:
	size_t				_cap;				//
	char*				_begin;				//内存块
	char*				_data_start;		//数据头;
	char*				_data_end;
};

cycle_memory_block::cycle_memory_block(size_t cap_):
	_cap(cap_),
	_begin(nullptr),
	_data_start(nullptr),
	_data_end(nullptr)

{
	_cap = cap_;
	_begin = (char*)malloc(cap_);
}

bool cycle_memory_block::append(void* data_, size_t len)
{
	if (len > cap() - size())
		return false;
	
	if (_data_end + len > end())
	{
		int c_len = end() - _data_end;
		memcpy(_data_end, data_, c_len);
		memcpy(_begin, (char*)data_ + c_len, len - c_len);
		
	}
	else
	{
		memcpy(_data_end + 1, data_, len);
	}

	return true;
}


void cycle_memory_block::pop(size_t size_)
{
	size_t len = size();
	if (size_ > len) {
		pop(len); 
		return; 
	} 
	//
	if (size_ == len) {
		clear();
		return;
	}
	//
	if (_data_start + size_ > _begin + _cap)
	{
		_data_start = _begin + size_ - (_begin + _cap - _data_start);
	}
	else 
	{
		_data_start += size_;
	}
}

size_t cycle_memory_block::c_length()
{
	if (_data_end < _data_start) 
		return end() + _cap - _data_start;
	return end() - data();
}

#endif