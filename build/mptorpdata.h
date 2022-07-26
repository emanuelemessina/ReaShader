#pragma once

namespace ReaShader {
	class RSData {

	public:
		RSData(size_t numElements);
		~RSData() {
			free(m_data);
		}

		void** allocateProcessorToReaperData(size_t numElements);

		bool push(void* ptr);

		void* getDataPtr() {
			return (void*)m_data;
		}

		void* get(unsigned short index);

	private:
		void** m_data;
		size_t size;
		unsigned short cursor{ 0 };

	};
}