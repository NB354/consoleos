CONSOLEOS_GAMEMGR_SITE = $(BR2_EXTERNAL_CONSOLEOS_PATH)/../src/gamemgr
CONSOLEOS_GAMEMGR_SITE_METHOD = local
CONSOLEOS_GAMEMGR_DEPENDENCIES = sqlite libarchive openssl

define CONSOLEOS_GAMEMGR_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CONSOLEOS_GAMEMGR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/consoleos-gamemgrd $(TARGET_DIR)/usr/bin/consoleos-gamemgrd
	$(INSTALL) -D -m 0755 $(@D)/mkzpk.py $(TARGET_DIR)/usr/bin/mkzpk
endef

$(eval $(generic-package))
