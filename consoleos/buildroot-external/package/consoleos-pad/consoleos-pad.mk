CONSOLEOS_PAD_SITE = $(BR2_EXTERNAL_CONSOLEOS_PATH)/../src/paddaemon
CONSOLEOS_PAD_SITE_METHOD = local
CONSOLEOS_PAD_DEPENDENCIES = libevdev

define CONSOLEOS_PAD_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CONSOLEOS_PAD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/consoleos-padd $(TARGET_DIR)/usr/bin/consoleos-padd
endef

$(eval $(generic-package))
