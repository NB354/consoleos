CONSOLEOS_UI_SITE = $(BR2_EXTERNAL_CONSOLEOS_PATH)/../src/ui
CONSOLEOS_UI_SITE_METHOD = local
CONSOLEOS_UI_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf sdl2_mixer sqlite

define CONSOLEOS_UI_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CONSOLEOS_UI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/consoleos-ui $(TARGET_DIR)/usr/bin/consoleos-ui
endef

$(eval $(generic-package))
