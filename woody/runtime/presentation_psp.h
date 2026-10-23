#ifndef WOODY_PRESENTATION_PSP_H
#define WOODY_PRESENTATION_PSP_H
void woody_presentation_boot(void);
void woody_presentation_loading(const char *level, const char *stage, float progress);
void woody_presentation_free(void);
void woody_presentation_present(void);
void woody_presentation_export_scene(const char *level);
#endif
