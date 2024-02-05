package=vdf
$(package)_version=v1.0.1
$(package)_download_path=https://github.com/depinc/vdf/archive/refs/tags/
$(package)_download_file=$($(package)_version).tar.gz
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=8022ce6c7f0ca7ad0d1f95ea5f248dc54869275c42a401fb618af9bf1055791a

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash))
endef

define $(package)_config_cmds
  cmake -DCMAKE_TOOLCHAIN_FILE=$(BASEDIR)/hosts/cmake/$(host).cmake -DBUILD_TEST=0 -DCMAKE_INSTALL_PREFIX=$(host_prefix) -DFETCHCONTENT_BASE_DIR=$($(package)_source_dir) .
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
