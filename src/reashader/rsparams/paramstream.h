/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include <clap/stream.h>

// Minimal binary reader/writer over CLAP's clap_ostream_t/clap_istream_t, providing just the
// handful of primitive operations rsparams serialization needs. Replaces the old
// IBStreamer-based one (VST3's stream helper) -- byte layout is unchanged, only the underlying
// stream type differs. Both clap_ostream_t::write and clap_istream_t::read may do partial
// I/O per call (per their own doc comments), so both loop until done or an error/EOF is hit.

namespace ReaShader::Parameters
{
	class ParamWriter
	{
	  public:
		explicit ParamWriter(const clap_ostream_t* stream) : stream(stream) {}

		bool writeInt8u(uint8_t v) { return writeRaw(&v, sizeof(v)); }
		bool writeInt32(int32_t v) { return writeRaw(&v, sizeof(v)); }
		bool writeInt32u(uint32_t v) { return writeRaw(&v, sizeof(v)); }
		bool writeDouble(double v) { return writeRaw(&v, sizeof(v)); }

		bool writeStr8(const std::string& s)
		{
			uint32_t len = (uint32_t)s.size();
			return writeInt32u(len) && (len == 0 || writeRaw(s.data(), len));
		}

	  private:
		const clap_ostream_t* stream;

		bool writeRaw(const void* data, size_t size)
		{
			const char* p = (const char*)data;
			size_t written = 0;
			while (written < size)
			{
				int64_t n = stream->write(stream, p + written, size - written);
				if (n <= 0)
					return false;
				written += (size_t)n;
			}
			return true;
		}
	};

	class ParamReader
	{
	  public:
		explicit ParamReader(const clap_istream_t* stream) : stream(stream) {}

		bool readInt8u(uint8_t& v) { return readRaw(&v, sizeof(v)); }
		bool readInt32(int32_t& v) { return readRaw(&v, sizeof(v)); }
		bool readInt32u(uint32_t& v) { return readRaw(&v, sizeof(v)); }
		bool readDouble(double& v) { return readRaw(&v, sizeof(v)); }

		bool readStr8(std::string& s)
		{
			uint32_t len = 0;
			if (!readInt32u(len))
				return false;
			s.resize(len);
			return len == 0 || readRaw(s.data(), len);
		}

	  private:
		const clap_istream_t* stream;

		bool readRaw(void* data, size_t size)
		{
			char* p = (char*)data;
			size_t got = 0;
			while (got < size)
			{
				int64_t n = stream->read(stream, p + got, size - got);
				if (n <= 0) // 0 = EOF, <0 = error -- either way we didn't get everything we needed
					return false;
				got += (size_t)n;
			}
			return true;
		}
	};
} // namespace ReaShader::Parameters
