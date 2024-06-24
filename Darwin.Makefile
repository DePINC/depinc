OUTPUTDIR=macos-build-outputdir
INSTALL_PREFIX=$(shell pwd)/$(OUTPUTDIR)
PKG_CONFIG_PATH=/usr/local/opt/openssl@3/lib/pkgconfig

BJ=j7

GMP_VERSION=6.3.0
DB4_VERSION=4.8.30
PLOG_VERSION=1.1.9
VDF_VERSION=1.0.1
BLS_SIGNATURES_VERSION=1.0.7
BLAKE3_VERSION=1.5.0

BDB_PREFIX="$(INSTALL_PREFIX)/db4"

OUTPUT_LOG_H=$(INSTALL_PREFIX)/include/plog/Log.h
OUTPUT_GMP_LIB=$(INSTALL_PREFIX)/lib/libgmp.a
OUTPUT_BLAKE3_LIB=$(INSTALL_PREFIX)/lib/libblake3.a
OUTPUT_TIMEVDF_LIB=$(INSTALL_PREFIX)/lib/libtime_vdf.a
OUTPUT_BLS_LIB=$(INSTALL_PREFIX)/lib/libbls.a
OUTPUT_DB4_LIB=$(INSTALL_PREFIX)/db4/lib/libdb.a $(INSTALL_PREFIX)/db4/lib/libdb_cxx.a

PKG_GMP=$(INSTALL_PREFIX)/gmp-$(GMP_VERSION).tar.xz
PKG_BLAKE3=$(INSTALL_PREFIX)/$(BLAKE3_VERSION).tar.gz
PKG_SIGNATURES=$(INSTALL_PREFIX)/$(BLS_SIGNATURES_VERSION).tar.gz
PKG_PLOG=$(INSTALL_PREFIX)/$(PLOG_VERSION).tar.gz
PKG_VDF=$(INSTALL_PREFIX)/v$(VDF_VERSION).tar.gz

OUTPUT_FILES=$(OUTPUT_LOG_H) $(OUTPUT_GMP_LIB) $(OUTPUT_BLAKE3_LIB) $(OUTPUT_TIMEVDF_LIB) $(OUTPUT_BLS_LIB) $(OUTPUT_DB4_LIB)

all: Makefile

configure: configure.ac
	./autogen.sh

$(OUTPUTDIR):
	mkdir -p $(INSTALL_PREFIX)

Makefile: $(OUTPUTDIR) $(OUTPUT_FILES) configure
	./configure LDFLAGS="-L$(INSTALL_PREFIX)/lib" CPPFLAGS="-I$(INSTALL_PREFIX)/include" CXXFLAGS="-I$(INSTALL_PREFIX)/include" BDB_LIBS="-L$(BDB_PREFIX)/lib -ldb_cxx-4.8" BDB_CFLAGS="-I$(BDB_PREFIX)/include" $(ADD_FLAGS)

$(OUTPUT_GMP_LIB): $(PKG_GMP)
	cd $(INSTALL_PREFIX) && tar xf $(PKG_GMP) && cd gmp-$(GMP_VERSION) && ./configure --prefix=$(INSTALL_PREFIX) && make -$(BJ) && make install

$(PKG_GMP):
	cd $(INSTALL_PREFIX) && wget https://gmplib.org/download/gmp/gmp-$(GMP_VERSION).tar.xz

$(OUTPUT_DB4_LIB):
	cd $(INSTALL_PREFIX) && CFLAGS="-Wno-error=implicit-function-declaration" ../contrib/install_db4.sh .

$(OUTPUT_BLAKE3_LIB): $(PKG_BLAKE3)
	cd $(INSTALL_PREFIX) && tar xf $(BLAKE3_VERSION).tar.gz && cd BLAKE3-$(BLAKE3_VERSION)/c && cmake . -B build -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) && cmake --build build -$(BJ) && cmake --install build

$(PKG_BLAKE3):
	cd $(INSTALL_PREFIX) && wget https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/$(BLAKE3_VERSION).tar.gz

$(OUTPUT_LOG_H): $(PKG_PLOG)
	cd $(INSTALL_PREFIX) && tar xf $(PLOG_VERSION).tar.gz -m && cd plog-$(PLOG_VERSION) && cmake . -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) -DPLOG_BUILD_SAMPLES=0 && cmake --build . -$(BJ) && cmake --install .

$(PKG_PLOG):
	cd $(INSTALL_PREFIX) && wget https://github.com/SergiusTheBest/plog/archive/refs/tags/$(PLOG_VERSION).tar.gz

$(OUTPUT_TIMEVDF_LIB): $(PKG_VDF)
	cd $(INSTALL_PREFIX) && tar xf v$(VDF_VERSION).tar.gz && cd vdf-$(VDF_VERSION) && mkdir -p build && cd build && cmake .. -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) && cmake --build . -$(BJ) && cmake --install .

$(PKG_VDF):
	cd $(INSTALL_PREFIX) && wget https://github.com/depinc/vdf/archive/refs/tags/v$(VDF_VERSION).tar.gz

$(OUTPUT_BLS_LIB): $(PKG_SIGNATURES)
	cd $(INSTALL_PREFIX) && tar xf $(BLS_SIGNATURES_VERSION).tar.gz && cd bls-signatures-$(BLS_SIGNATURES_VERSION) && patch -ruN -d . < ../../depends/patches/bls_signatures/001-build-as-static-library.patch && cmake . -B build -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) -DBUILD_BLS_PYTHON_BINDINGS=0 -DBUILD_BLS_TESTS=0 -DBUILD_BLS_BENCHMARKS=0 && cmake --build build -$(BJ) && cmake --install build

$(PKG_SIGNATURES):
	cd $(INSTALL_PREFIX) && wget https://github.com/Chia-Network/bls-signatures/archive/refs/tags/$(BLS_SIGNATURES_VERSION).tar.gz

.PYONY: clean

clean:
	rm -rf configure Makefile $(OUTPUTDIR)

