#include <cstdlib>

#include "mptorpdata.h"

namespace ReaShader {

	RSData::RSData(size_t numElements) {
		m_data = allocateProcessorToReaperData(numElements);
	}

	void** RSData::allocateProcessorToReaperData(size_t numElements) {
		size = numElements;
		return (void**)malloc(numElements * sizeof(void*)); // allocate n slots for n pointers
	}

	bool RSData::push(void* ptr) {
		if (cursor >= size)
			return false;

		m_data[cursor] = (void*)ptr;

		cursor++;

		return true;
	}

	void* RSData::get(unsigned short index) {
		if (index >= size)
			return nullptr;

		return m_data[index];
	}
}