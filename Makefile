QMAKE_TARGET  = BBCord
PROJECT_DIR	  := $(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
I18N_DIR	  := $(PROJECT_DIR)/translations

include mk/cs-base.mk

# Build the headless service together with the UI app. bar-descriptor.xml packages
# ../BBCordHeadless/<arm|x86>/BBCordHeadless, so it must be up to date before packaging.
# Hooked onto every build target that cs-base.mk defines; the sub-make inherits the -I flag.
HEADLESS_DIR := $(PROJECT_DIR)../BBCordHeadless
define BUILD_HEADLESS_RULE
.PHONY: headless-$(1)
headless-$(1):
	$$(MAKE) -C "$(HEADLESS_DIR)" $(1)
$(1): headless-$(1)
endef
$(foreach t,Device-Debug Device-Release Device-Profile Simulator-Debug,$(eval $(call BUILD_HEADLESS_RULE,$(t))))
