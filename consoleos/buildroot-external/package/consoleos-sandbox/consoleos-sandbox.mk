CONSOLEOS_SANDBOX_SITE = $(BR2_EXTERNAL_CONSOLEOS_PATH)/../src/sandbox
CONSOLEOS_SANDBOX_SITE_METHOD = local

define CONSOLEOS_SANDBOX_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CONSOLEOS_SANDBOX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 4755 $(@D)/consoleos-sandbox $(TARGET_DIR)/usr/bin/consoleos-sandbox
endef

$(eval $(generic-package))
