
#include "pch.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <map>
#include <vector> 

#define CODE_SECION_NUM 10

/* LEB Encoding and Decoding code taken from WABT: https://github.com/WebAssembly/wabt/blob/master/src/leb128.cc */
#define MAX_U32_LEB128_BYTES 5
#define BYTE_AT(type, i, shift) ((static_cast<type>(p[i]) & 0x7f) << (shift))

#define LEB128_1(type) (BYTE_AT(type, 0, 0))
#define LEB128_2(type) (BYTE_AT(type, 1, 7) | LEB128_1(type))
#define LEB128_3(type) (BYTE_AT(type, 2, 14) | LEB128_2(type))
#define LEB128_4(type) (BYTE_AT(type, 3, 21) | LEB128_3(type))
#define LEB128_5(type) (BYTE_AT(type, 4, 28) | LEB128_4(type))
#define LEB128_6(type) (BYTE_AT(type, 5, 35) | LEB128_5(type))
#define LEB128_7(type) (BYTE_AT(type, 6, 42) | LEB128_6(type))
#define LEB128_8(type) (BYTE_AT(type, 7, 49) | LEB128_7(type))
#define LEB128_9(type) (BYTE_AT(type, 8, 56) | LEB128_8(type))
#define LEB128_10(type) (BYTE_AT(type, 9, 63) | LEB128_9(type))

#define LEB128_LOOP_UNTIL(end_cond) \
  do {                              \
    uint8_t byte = value & 0x7f;    \
    value >>= 7;                    \
    if (end_cond) {                 \
      data[length++] = byte;        \
      break;                        \
    } else {                        \
      data[length++] = byte | 0x80; \
    }                               \
  } while (1)

size_t ReadU32Leb128(const uint8_t* p,
	const uint8_t* end,
	uint32_t* out_value) {
	if (p < end && (p[0] & 0x80) == 0) {
		*out_value = LEB128_1(uint32_t);
		return 1;
	}
	else if (p + 1 < end && (p[1] & 0x80) == 0) {
		*out_value = LEB128_2(uint32_t);
		return 2;
	}
	else if (p + 2 < end && (p[2] & 0x80) == 0) {
		*out_value = LEB128_3(uint32_t);
		return 3;
	}
	else if (p + 3 < end && (p[3] & 0x80) == 0) {
		*out_value = LEB128_4(uint32_t);
		return 4;
	}
	else if (p + 4 < end && (p[4] & 0x80) == 0) {
		// The top bits set represent values > 32 bits.
		if (p[4] & 0xf0) {
			return 0;
		}
		*out_value = LEB128_5(uint32_t);
		return 5;
	}
	else {
		// past the end.
		*out_value = 0;
		return 0;
	}
}

 std::vector<unsigned char> WriteU32Leb128(uint32_t value, uint32_t *num) {
	uint8_t data[MAX_U32_LEB128_BYTES];
	int length = 0;
	LEB128_LOOP_UNTIL(value == 0);
	*num = length;

	std::vector<unsigned char> returnAr;
	for (int i = 0; i < length; i++) {
		returnAr.push_back(data[i]);
	}
	
	return returnAr;
}

 class FileBuffer {
 private:
	 std::vector<unsigned char> byteBuffer;
	 size_t position = 0;
	 unsigned short oneInFront = (unsigned short)0x80;

	bool matchesSpecialOpcodes(unsigned char character){
		unsigned char specialChars[] = { (unsigned char)0x41, (unsigned char)0x42, (unsigned char)0x43, (unsigned char)0x44,(unsigned char) 0x10};

		for(int i = 0; i < 5;i++){
			if(character == specialChars[i]){
				return true;
			}
		}
		return false;
	}
 public:
	 size_t size;

	 FileBuffer(char * buffer, size_t length) {
		 size = length;
		 for (int i = 0; i < length; i++) {
			 byteBuffer.push_back((unsigned char)buffer[i]);
		 }
	 }

	 void resetPosition() {
		 position = 0;
	 }

	 unsigned char getNextByte() {
		 unsigned char byte = (unsigned char) byteBuffer[position];
		 position += 1;
		 return byte;
	 }

	 bool hasNext() {
		 return position < size;
	 }

	
	 bool isPartOfLEBInteger() {
		 for (int k = 0; k < 5; k++) {
			 unsigned char previousChar = (unsigned char)byteBuffer[(position - 1) - k];
			 unsigned short andResult = previousChar & oneInFront;
			 if (andResult == 0) { //if its not a LEB integer, break out
				 if (matchesSpecialOpcodes(previousChar)) {
						 //make sure this char isn't actually part of another const
						 for(int n = 1; n < 4; n++){
							unsigned char innerPreviousChar = (unsigned char)byteBuffer[(position - 1) - k - n];
							unsigned short innerAndResult = innerPreviousChar & oneInFront;
							 if (innerAndResult == 0) { 
								if (matchesSpecialOpcodes(innerPreviousChar)) {
									return false; //Current Op is not part of leb integer
								} else {
									return true;
								}
								return true;
							 }
						 }

					 return true;
				 }
				 return false;
			 }
		 }

		 return true;
	 }



 };

 uint32_t ReadNextLEB(FileBuffer *byteBuffer, uint32_t* originalEncodedSeize, std::ofstream *outfile) {
	 char c;
	 unsigned short oneInFront = (unsigned short)0x80;
	 unsigned char lebEncoded[10] = { 0,0,0,0,0,0,0,0,0,0 };
	 int idx = 0;
	 
	 while (true) {
		 c = byteBuffer->getNextByte();

		 unsigned short bite = (unsigned short)c;
		 lebEncoded[idx] = bite;
		 if (outfile != nullptr) {
			 printf("%02x ", (unsigned char)c);
			 outfile->write(&c, 1);
		 }
		 idx++;
		 *originalEncodedSeize += 1;

		 unsigned short andResult = bite & oneInFront;
		 if (andResult == 0) {
			 break;
		 }
	 }

	 const uint8_t *start = lebEncoded;
	 const uint8_t *end = lebEncoded + idx;
	 uint32_t lebValue = 0;
	 ReadU32Leb128(start, end, &lebValue);
	 return lebValue ;

 }

int main(int argc, char *argv[])
{
	const char* standardConformingWasm = ".\\72b2c42dbaadbbb8.ok.wasm";
	const char* asmContainingWasm = ".\\7804144ad8232ae7.ok.wasm";
	
	char * inputFilename = nullptr;

	if (argc > 1) {
		inputFilename = argv[1];
	}
	if (inputFilename == nullptr) {
		inputFilename = (char *)asmContainingWasm;
	}

	char * outputFileName = nullptr;
	if (argc > 2) {
		outputFileName = argv[2];
	}
	if (outputFileName == nullptr) {
		outputFileName =  (char *)"out.wasm";
	}

	std::ifstream infile(inputFilename, std::ifstream::binary);
	infile.seekg(0, infile.end);
	size_t length = infile.tellg();
	infile.seekg(0, infile.beg);

	char * buffer = new char[length];
	std::map<unsigned char,  unsigned char*> asmJsOpCodes;
	asmJsOpCodes[0xc5] = new unsigned char[1]{ 0x9f }; // F64ArcCosine to F64 Sqrt
	asmJsOpCodes[0xc6] = new unsigned char[1]{ 0x9f }; // F64ArcSine to F64 Sqrt
	asmJsOpCodes[0xc7] = new unsigned char[1]{ 0x9f }; // F64ArcTan to F64 Sqrt
	asmJsOpCodes[0xc8] = new unsigned char[1]{ 0x9f }; // F64Cosine to F64 Sqrt
	asmJsOpCodes[0xc9] = new unsigned char[1]{ 0x9f }; // F64Sine to F64 Sqrt
	asmJsOpCodes[0xca] = new unsigned char[1]{ 0x9f }; // F64Tan to F64 Sqrt
	asmJsOpCodes[0xcb] = new unsigned char[1]{ 0x9f }; // F64Exp to F64 Sqrt
	asmJsOpCodes[0xcc] = new unsigned char[1]{ 0x9f }; // F64Log to F64 Sqrt

	asmJsOpCodes[0xcd] = new unsigned char[1]{ 0xa3 }; // F64Atan2 to F64 Div
	asmJsOpCodes[0xce] = new unsigned char[1]{ 0xa3 }; // F64Pow to F64 Div
	asmJsOpCodes[0xcf] = new unsigned char[1]{ 0xa3 }; // F64Mod to F64 Div
	
	asmJsOpCodes[0xd3] = new unsigned char[1]{ 0x6d }; // I32AsmjsDivS to I32DivS
	asmJsOpCodes[0xd4] = new unsigned char[1]{ 0x6e }; // I32AsmjsDivU to I32DivU
	asmJsOpCodes[0xd5] = new unsigned char[1]{ 0x6f }; // I32AsmjsRemS to I32RemS
	asmJsOpCodes[0xd6] = new unsigned char[1]{ 0x70 }; // I32AsmjsRemU to I32RemU

	asmJsOpCodes[0xd7] = new unsigned char[3]{ 0x2c, 0x00,0x00 }; // I32AsmjsLoadMem8S
	asmJsOpCodes[0xd8] = new unsigned char[3]{ 0x2d, 0x00,0x00 }; // I32AsmjsLoadMem8U
	asmJsOpCodes[0xd9] = new unsigned char[3]{ 0x2e, 0x01,0x00 }; // I32AsmjsLoadMem16S
	asmJsOpCodes[0xda] = new unsigned char[3]{ 0x2f, 0x01,0x00 }; // I32AsmjsLoadMem16U
	asmJsOpCodes[0xdb] = new unsigned char[3]{ 0x28, 0x02,0x00 }; // I32AsmjsLoadMem
	asmJsOpCodes[0xdc] = new unsigned char[3]{ 0x2a, 0x02,0x00 }; // F32AsmjsLoadMem
	asmJsOpCodes[0xdd] = new unsigned char[3]{ 0x2b, 0x04,0x00 }; // F64AsmjsLoadMem

	asmJsOpCodes[0xde] = new unsigned char[3]{ 0x3a, 0x00,0x00 }; // I32AsmjsStoreMem8
	asmJsOpCodes[0xdf] = new unsigned char[3]{ 0x3b, 0x01,0x00 }; // I32AsmjsStoreMem16
	asmJsOpCodes[0xe0] = new unsigned char[3]{ 0x36, 0x02,0x00 }; // I32AsmjsStoreMem
	asmJsOpCodes[0xe1] = new unsigned char[3]{ 0x38, 0x02,0x00 }; // F32AsmjsStoreMem
	asmJsOpCodes[0xe2] = new unsigned char[3]{ 0x39, 0x04,0x00 }; // F64AsmjsStoreMem

	asmJsOpCodes[0xe3] = new unsigned char[1]{ 0xa8 }; // I32AsmjsSConvertF32
	asmJsOpCodes[0xe4] = new unsigned char[1]{ 0xa9}; // I32AsmjsUConvertF32
	asmJsOpCodes[0xe5] = new unsigned char[1]{ 0xaa}; // I32AsmjsSConvertF64
	asmJsOpCodes[0xe6] = new unsigned char[1]{ 0xab}; // I32AsmjsUConvertF64

	std::map<unsigned char, int> asmJsOpCodesNumOfCodes;
	asmJsOpCodesNumOfCodes[0xc5] = 1;
	asmJsOpCodesNumOfCodes[0xc6] = 1;
	asmJsOpCodesNumOfCodes[0xc7] = 1;
	asmJsOpCodesNumOfCodes[0xc8] = 1;
	asmJsOpCodesNumOfCodes[0xc9] = 1;
	asmJsOpCodesNumOfCodes[0xca] = 1;
	asmJsOpCodesNumOfCodes[0xcb] = 1;
	asmJsOpCodesNumOfCodes[0xcc] = 1;
	asmJsOpCodesNumOfCodes[0xcd] = 1;
	asmJsOpCodesNumOfCodes[0xce] = 1;
	asmJsOpCodesNumOfCodes[0xcf] = 1;

	asmJsOpCodesNumOfCodes[0xd3] = 1;
	asmJsOpCodesNumOfCodes[0xd4] = 1;
	asmJsOpCodesNumOfCodes[0xd5] = 1;
	asmJsOpCodesNumOfCodes[0xd6] = 1;

	asmJsOpCodesNumOfCodes[0xd7] = 3;
	asmJsOpCodesNumOfCodes[0xd8] = 3;
	asmJsOpCodesNumOfCodes[0xd9] = 3;
	asmJsOpCodesNumOfCodes[0xda] = 3;
	asmJsOpCodesNumOfCodes[0xdb] = 3;
	asmJsOpCodesNumOfCodes[0xdc] = 3;
	asmJsOpCodesNumOfCodes[0xdd] = 3;

	asmJsOpCodesNumOfCodes[0xde] = 3;
	asmJsOpCodesNumOfCodes[0xdf] = 3;
	asmJsOpCodesNumOfCodes[0xe0] = 3;
	asmJsOpCodesNumOfCodes[0xe1] = 3;
	asmJsOpCodesNumOfCodes[0xe2] = 3;

	asmJsOpCodesNumOfCodes[0xe3] = 1;
	asmJsOpCodesNumOfCodes[0xe4] = 1;
	asmJsOpCodesNumOfCodes[0xe5] = 1;
	asmJsOpCodesNumOfCodes[0xe6] = 1;

	infile.read(buffer, length);

	FileBuffer byteBuffer(buffer, length);

	free(buffer);

	std::map<short, uint32_t> sectionSizes;
	std::map<short, int> originalSectionLengthSizes;
	char c;
	unsigned short oneInFront = (unsigned short)0x80;

	int totalCodeSectionSizeIncrease = 0;
	std::vector<std::map<char, uint32_t>> functionDetails;
	uint32_t numOfFunctions = 0;
	for (int i = 0; i < 8; i++) {
		//Magic section
		byteBuffer.getNextByte();
	}


	while (byteBuffer.hasNext()) {
		c = byteBuffer.getNextByte();
		
		short sectionType = (short)c;

		if (!byteBuffer.hasNext()) {
			//possible if section lenght is ommitted and no data
			sectionSizes[sectionType] = 0;
			originalSectionLengthSizes[sectionType] = 0;
			break;
		}
		//Get section length
		uint32_t originalSectionLengthEncodedSize = 0;

		uint32_t lebValue = ReadNextLEB(&byteBuffer, &originalSectionLengthEncodedSize, nullptr);

		sectionSizes[sectionType] = lebValue;
		originalSectionLengthSizes[sectionType] = originalSectionLengthEncodedSize;

		int i = 0;

		if (sectionType == CODE_SECION_NUM) {
			//Get number of function
			//c = byteBuffer.getNextByte();

			int functionIndex = 0;
			uint32_t encodingForNumFunctions = 0;
			numOfFunctions = ReadNextLEB(&byteBuffer, &encodingForNumFunctions, nullptr);

			printf("Number of Functions: %d\n", numOfFunctions);
			for (; functionIndex < numOfFunctions; functionIndex++) {
				uint32_t originalEncodedFunctionSizeLength = 0;
				uint32_t functionSize = ReadNextLEB(&byteBuffer, &originalEncodedFunctionSizeLength,nullptr);
				printf("Function %i size: %d (%d)\n", functionIndex, functionSize, originalEncodedFunctionSizeLength);
				std::map<char, uint32_t> functionSizes;
				functionDetails.push_back(functionSizes);
				functionDetails[functionIndex]['s'] = functionSize;
				functionDetails[functionIndex]['o'] = originalEncodedFunctionSizeLength;

				uint32_t functionByteIndex = 0;
				bool previousWasAsmStore = false;
				while (functionByteIndex++ < functionSize) {

					c = byteBuffer.getNextByte();
					unsigned char bite = (unsigned char)c;

					//check for Drops after asmjs Stores 
					if (bite == (unsigned char) 0x1a && previousWasAsmStore) {
						totalCodeSectionSizeIncrease -= 1;
						functionDetails[functionIndex]['s'] -= 1;
						previousWasAsmStore = false;
					}

					unsigned char * mappedValues = asmJsOpCodes[bite];
					if (mappedValues != nullptr) {
						bool isLEBInteger = byteBuffer.isPartOfLEBInteger();
						//check if opcode is opcode or LEB integer
						if (!isLEBInteger) {
							int lengthOfOpCodes = asmJsOpCodesNumOfCodes[bite];

							totalCodeSectionSizeIncrease += (lengthOfOpCodes - 1);
							functionDetails[functionIndex]['s'] += (lengthOfOpCodes - 1);
							if (lengthOfOpCodes == 3) {
								previousWasAsmStore = true;
							}
							else {
								previousWasAsmStore = false;;
							}
						}
						else {
							previousWasAsmStore = false;
						}
					}
					else {
						previousWasAsmStore = false;;
					}
				}
				//get length of new function size encoding
				uint32_t newFunctionEncodingSize = 0;
				WriteU32Leb128(functionDetails[functionIndex]['s'], &newFunctionEncodingSize);
				totalCodeSectionSizeIncrease += ( newFunctionEncodingSize - functionDetails[functionIndex]['o']);
			}


			while (byteBuffer.hasNext()) {
				c = byteBuffer.getNextByte();
			}
			sectionSizes[sectionType] = lebValue + totalCodeSectionSizeIncrease;
		}
		else {
			while (i++ < lebValue ) {
				c = byteBuffer.getNextByte();
			}
		}

		//int encodedLebLength = 0;

		//std::vector<unsigned char> encodedLeb = WriteU32Leb128(sectionSizes[sectionType], &encodedLebLength);

		/*if (originalSectionLengthEncodedSize != encodedLebLength) {
			sectionSizes[sectionType] = sectionSizes[sectionType] + (encodedLebLength - originalSectionLengthEncodedSize);
		}
		*/
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Read file again, replacing this time
	std::ofstream fout;
	byteBuffer.resetPosition();
	fout.open(outputFileName, std::ios::binary | std::ios::out);

	for (int i = 0; i < 8; i++) {
		//Magic section
		c = byteBuffer.getNextByte();
		printf("%02x ", c);
		fout.write(&c, 1);
		continue;
	}

	while (byteBuffer.hasNext()) {
		c = byteBuffer.getNextByte();

		if (!byteBuffer.hasNext() ) { //last section empty
			printf("%02x ", c);
			fout.write(&c, 1);
			continue;
		}


		printf("%02x ", (unsigned char)c);
		fout.write(&c, 1);

		short sectionType = (short)c;

		if (!byteBuffer.hasNext()) {
			//possible if section lenght is ommitted and no data
			break;
		}

		//Get section length
		uint32_t lebValue = 0;
		uint32_t encodedLebLength = 0;
		if (sectionType == CODE_SECION_NUM) {
			lebValue = sectionSizes[sectionType];
			
			std::vector<unsigned char> encodedLeb = WriteU32Leb128(lebValue, &encodedLebLength);
			for (uint8_t j = 0; j < encodedLebLength; j++) {
				c = encodedLeb[j];
				printf("%02x ", (unsigned char)c);
				fout.write(&c, 1);

			}
			//skip over original section length bytes
			int originalSectionSize = originalSectionLengthSizes[sectionType];
			for (int i = 0; i < originalSectionSize; i++) {
				byteBuffer.getNextByte();
			}
		}
		else {
			unsigned char lebEncoded[10] = { 0,0,0,0,0,0,0,0,0,0 };
			int idx = 0;
			while (true) {
				c = byteBuffer.getNextByte();

				unsigned short bite = (unsigned short)c;
				lebEncoded[idx] = bite;
				printf("%02x ", (unsigned char)c);
				fout.write(&c, 1);
				idx++;

				unsigned short andResult = bite & oneInFront;
				if (andResult == 0) {
					break;
				}
			}

			const uint8_t *start = lebEncoded;
			const uint8_t *end = lebEncoded + idx;
			lebValue = 0;
			size_t leb = ReadU32Leb128(start, end, &lebValue);
		}

		
		int i = 0;
		if (sectionType == CODE_SECION_NUM) {
			int functionIndex = 0;
			uint32_t encodingForNumFunctions = 0;
			numOfFunctions = ReadNextLEB(&byteBuffer, &encodingForNumFunctions, &fout);

			for (; functionIndex < numOfFunctions; functionIndex++) {
				uint32_t originalEncodedFunctionSizeLength = 0;
				uint32_t functionSize = ReadNextLEB(&byteBuffer, &originalEncodedFunctionSizeLength, nullptr);
				//overwrite function body size

				uint32_t encodedFunctionSizeLength = 0;
				std::vector<unsigned char> encodedLeb = WriteU32Leb128(functionDetails[functionIndex]['s'], &encodedFunctionSizeLength);
				for (uint8_t j = 0; j < encodedFunctionSizeLength; j++) {
					c = encodedLeb[j];
					printf("%02x ", (unsigned char)c);
					fout.write(&c, 1);

				}

				uint32_t functionByteIndex = 0;
				bool previousWasAsmStore = false;

				while (functionByteIndex++ < functionSize) {
					c = byteBuffer.getNextByte();
					unsigned char bite = (unsigned char)c;
					//check for Drops after asmjs Stores 
					if (bite == (unsigned char)0x1a && previousWasAsmStore) {
						previousWasAsmStore = false;
						continue;
					}

					unsigned char * mappedValues = asmJsOpCodes[bite];
					if (mappedValues != nullptr) { //matched Asm.js opcode
						bool isLEBInteger = byteBuffer.isPartOfLEBInteger();
						//check if opcode is opcode or LEB integer
						if (!isLEBInteger) {
							int lengthOfOpCodes = asmJsOpCodesNumOfCodes[bite];
							for (int i = 0; i < lengthOfOpCodes; i++) {
								printf("%02x ", (unsigned char)mappedValues[i]);
								const char writeVal = mappedValues[i];
								fout.write(&writeVal, 1);
							}
							if (lengthOfOpCodes == 3) {
								previousWasAsmStore = true;
							}
							else {
								previousWasAsmStore = false;;
							}
						}
						else {
							previousWasAsmStore = false;
							printf("%02x ", bite);
							fout.write(&c, 1);
						}

						
					}
					else {
						previousWasAsmStore = false;
						printf("%02x ", bite);
						fout.write(&c, 1);
					}
				}

			}


			while (byteBuffer.hasNext()) {
				c = byteBuffer.getNextByte();
				unsigned char bite = (unsigned char)c;
				printf("%02x ", bite);
				fout.write(&c, 1);
			}

		}
		else {
			while (i++ < lebValue) {
				c = byteBuffer.getNextByte();
				unsigned char bite = (unsigned char)c;
				printf("%02x ", bite);
				fout.write(&c, 1);
			}
		}
	}
	
	fout.close();

	infile.close();
	return 0;
}