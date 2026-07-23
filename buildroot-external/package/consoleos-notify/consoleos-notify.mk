CONSOLEOS_NOTIFY_SITE = $(BR2_EXTERNAL_CONSOLEOS_PATH)/../src/common
CONSOLEOS_NOTIFY_SITE_METHOD = local

define CONSOLEOS_NOTIFY_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) notifyd
endef

define CONSOLEOS_NOTIFY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/consoleos-notifyd $(TARGET_DIR)/usr/bin/consoleos-notifyd
endef

$(eval $(generic-package))
