CONSOLEOS_UPDATE_SITE = $(BR2_EXTERNAL_CONSOLEOS_PATH)/../src/updatetool
CONSOLEOS_UPDATE_SITE_METHOD = local
CONSOLEOS_UPDATE_DEPENDENCIES = libsodium

define CONSOLEOS_UPDATE_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CONSOLEOS_UPDATE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/consoleos-updated $(TARGET_DIR)/usr/bin/consoleos-updated
endef

$(eval $(generic-package))
