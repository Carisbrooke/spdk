SPDK_ROOT_DIR := $(abspath $(CURDIR)/..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

APP = bdev_pls

C_SRCS := bdev_pls.c

SPDK_LIB_LIST = event_bdev event_copy
SPDK_LIB_LIST += bdev copy event trace log conf util rpc jsonrpc json

LIBS += $(BLOCKDEV_MODULES_LINKER_ARGS) \
        $(COPY_MODULES_LINKER_ARGS)

LIBS += $(SPDK_LIB_LINKER_ARGS) $(ENV_LINKER_ARGS)

all : $(APP)
        @:

$(APP) : $(OBJS) $(SPDK_LIB_FILES) $(BLOCKDEV_MODULES_FILES) $(LINKER_MODULES) $(ENV_LIBS) $(LINK_C)

include $(SPDK_ROOT_DIR)/mk/spdk.deps.mk
