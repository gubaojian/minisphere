#ifndef MINISPHERE__INPUT_H__INCLUDED
#define MINISPHERE__INPUT_H__INCLUDED

void  initialize_keyboard  (void);
void  shutdown_keyboard    (void);
bool  is_any_key_down      (void);
bool  is_key_down          (int keycode);
bool  is_key_toggled       (int keycode);
void  attach_input_display (void);
void  clear_key_queue      (void);
int   get_num_keys         (void);
int   read_key             (void);
void  update_keyboard      (void);

#endif // MINISPHERE__INPUT_H__INCLUDED
