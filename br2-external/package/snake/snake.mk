################################################################################
#
# snake
#
################################################################################

SNAKE_VERSION = 1.0
SNAKE_SITE = $(BR2_EXTERNAL_RPI_SNAKE_PATH)/../src
SNAKE_SITE_METHOD = local
SNAKE_LICENSE = MIT

define SNAKE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -Wall $(TARGET_LDFLAGS) \
		-o $(@D)/snake $(@D)/snake.c
endef

define SNAKE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/snake $(TARGET_DIR)/usr/bin/snake
endef

$(eval $(generic-package))
