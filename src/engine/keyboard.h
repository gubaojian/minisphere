#ifndef MINISPHERE__INPUT_H__INCLUDED
#define MINISPHERE__INPUT_H__INCLUDED

void  initialize_keyboard  (void);
void  shutdown_keyboard    (void);
int   kb_num_keys          (void);
void  kb_attach_display    (void);
void  kb_clear_queue       (void);
int   kb_get_key           (void);
bool  kb_is_any_key_down   (void);
bool  kb_is_key_down       (int keycode);
bool  kb_is_toggled        (int keycode);
void  kb_update            (void);

#endif // MINISPHERE__INPUT_H__INCLUDED
