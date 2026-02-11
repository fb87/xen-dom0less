// vim: tabstop=4 shiftwidth=4 expandtab colorcolumn=80 autoindent
#include <stdio.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lvgl/src/drivers/display/drm/lv_linux_drm.h"

int main(int argc, char ** argv) {
    lv_init();

    fprintf(stdout, "Find the DRM path\n");

    char *device = lv_linux_drm_find_device_path();
    if (device == NULL) {
        fprintf(stderr, "Error: No connected DRM device found.\n");
        return -1;
    }

    fprintf(stdout, "Create display\n");
    lv_display_t *disp = lv_linux_drm_create();
    
    fprintf(stdout, "Set file!\n");
    lv_linux_drm_set_file(disp, device, -1);
    
    fprintf(stdout, "Create the demo %s\n", argv[1]);
    if (!lv_demos_create(&argv[1], argc - 1)) {
        fprintf(stderr, "Failed! to create the demo\n");
        lv_demos_show_help();
        goto demo_end;
    }

    fprintf(stdout, "Event loop...\n");
    while (1) {
        uint32_t time_until_next = lv_timer_handler();
        
        // If no timer is ready, use the default refresh period for delay
        if(time_until_next == LV_NO_TIMER_READY) {
            time_until_next = LV_DEF_REFR_PERIOD;
        }
        
        // Delay the execution until the next timer event
        lv_delay_ms(time_until_next);
    }

demo_end:
    lv_deinit();
    return 0;
}

