/*  chat_commands.c
 *
 *
 *  Copyright (C) 2014 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "chat.h"
#include "conference.h"
#include "execute.h"
#include "file_transfers.h"
#include "friendlist.h"
#include "line_info.h"
#include "groupchats.h"
#include "misc_tools.h"
#include "toxic.h"
#include "windows.h"

extern ToxWindow *prompt;
extern FriendsList Friends;

void cmd_cancelfile(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    UNUSED_VAR(window);

    if (argc < 2) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Requires type in|out and the file ID.");
        return;
    }

    char msg[MAX_STR_SIZE];
    const char *inoutstr = argv[1];
    long int idx = strtol(argv[2], NULL, 10);

    if ((idx == 0 && strcmp(argv[2], "0")) || idx >= MAX_FILES || idx < 0) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid file ID.");
        return;
    }

    struct FileTransfer *ft = NULL;

    /* cancel an incoming file transfer */
    if (strcasecmp(inoutstr, "in") == 0) {
        ft = get_file_transfer_struct_index(self->num, idx, FILE_TRANSFER_RECV);
    } else if (strcasecmp(inoutstr, "out") == 0) {
        ft = get_file_transfer_struct_index(self->num, idx, FILE_TRANSFER_SEND);
    } else {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Type must be 'in' or 'out'.");
        return;
    }

    if (!ft) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid file ID.");
        return;
    }

    if (ft->state == FILE_TRANSFER_INACTIVE) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid file ID.");
        return;
    }

    snprintf(msg, sizeof(msg), "File transfer for '%s' aborted.", ft->file_name);
    close_file_transfer(self, m, ft, TOX_FILE_CONTROL_CANCEL, msg, silent);
}

void cmd_conference_invite(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    UNUSED_VAR(window);

    if (argc < 1) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Conference number required.");
        return;
    }

    long int conferencenum = strtol(argv[1], NULL, 10);

    if ((conferencenum == 0 && strcmp(argv[1], "0")) || conferencenum < 0 || conferencenum == LONG_MAX) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid conference number.");
        return;
    }

    Tox_Err_Conference_Invite err;

    if (!tox_conference_invite(m, self->num, conferencenum, &err)) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to invite contact to conference (error %d)", err);
        return;
    }

    line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invited contact to Conference %ld.", conferencenum);
}

void cmd_conference_join(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    UNUSED_VAR(window);
    UNUSED_VAR(argc);
    UNUSED_VAR(argv);

    if (get_num_active_windows() >= MAX_WINDOWS_NUM) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, RED, " * Warning: Too many windows are open.");
        return;
    }

    const char *conferencekey = Friends.list[self->num].conference_invite.key;
    uint16_t length = Friends.list[self->num].conference_invite.length;
    uint8_t type = Friends.list[self->num].conference_invite.type;

    if (!Friends.list[self->num].conference_invite.pending) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "No pending conference invite.");
        return;
    }

    uint32_t conferencenum;

    if (type == TOX_CONFERENCE_TYPE_TEXT) {
        Tox_Err_Conference_Join err;
        conferencenum = tox_conference_join(m, self->num, (const uint8_t *) conferencekey, length, &err);

        if (err != TOX_ERR_CONFERENCE_JOIN_OK) {
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Conference instance failed to initialize (error %d)", err);
            return;
        }
    } else if (type == TOX_CONFERENCE_TYPE_AV) {
#ifdef AUDIO
        conferencenum = toxav_join_av_groupchat(m, self->num, (const uint8_t *) conferencekey, length,
                                                audio_conference_callback, NULL);

        if (conferencenum == (uint32_t) -1) {
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Audio conference instance failed to initialize");
            return;
        }

#else
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Audio support disabled by compile-time option.");
        return;
#endif
    } else {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Unknown conference type %d", type);
        return;
    }

    if (init_conference_win(m, conferencenum, type, NULL, 0) == -1) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Conference window failed to initialize.");
        tox_conference_delete(m, conferencenum, NULL);
        return;
    }

#ifdef AUDIO

    if (type == TOX_CONFERENCE_TYPE_AV) {
        if (!init_conference_audio_input(m, conferencenum)) {
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Audio capture failed; use \"/audio on\" to try again.");
        }
    }

#endif
}

void cmd_group_accept(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    if (get_num_active_windows() >= MAX_WINDOWS_NUM) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, RED, " * Warning: Too many windows are open.");
        return;
    }

    if (Friends.list[self->num].group_invite.length == 0) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "No pending group invite");
        return;
    }

    const char *passwd = NULL;
    uint16_t passwd_len = 0;

    if (argc > 0) {
        passwd = argv[1];
        passwd_len = strlen(passwd);
    }

    size_t nick_len = tox_self_get_name_size(m);
    char self_nick[TOX_MAX_NAME_LENGTH + 1];
    tox_self_get_name(m, (uint8_t *) self_nick);
    self_nick[nick_len] = '\0';

    TOX_ERR_GROUP_INVITE_ACCEPT err;
    uint32_t groupnumber = tox_group_invite_accept(m, self->num, Friends.list[self->num].group_invite.data,
                           Friends.list[self->num].group_invite.length, (const uint8_t *) self_nick, nick_len,
                           (const uint8_t *) passwd, passwd_len, &err);

    if (err != TOX_ERR_GROUP_INVITE_ACCEPT_OK) {
        if (err == TOX_ERR_GROUP_INVITE_ACCEPT_TOO_LONG) {
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to join group: Password too long.");
        } else {
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to join group (error %d).", err);
        }

        return;
    }

    if (init_groupchat_win(m, groupnumber, NULL, 0, Group_Join_Type_Join) == -1) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Group chat window failed to initialize.");
        tox_group_leave(m, groupnumber, NULL, 0, NULL);
        return;
    }
}

void cmd_group_invite(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    if (argc < 1) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Group number required.");
        return;
    }

    int groupnumber = atoi(argv[1]);

    if (groupnumber == 0 && strcmp(argv[1], "0")) {    /* atoi returns 0 value on invalid input */
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid group number.");
        return;
    }

    TOX_ERR_GROUP_INVITE_FRIEND err;

    if (!tox_group_invite_friend(m, groupnumber, self->num, &err)) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to invite contact to group (error %d).", err);
        return;
    }

    line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invited contact to Group %d.", groupnumber);
}

void cmd_savefile(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    UNUSED_VAR(window);

    if (argc < 1) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File ID required.");
        return;
    }

    long int idx = strtol(argv[1], NULL, 10);

    if ((idx == 0 && strcmp(argv[1], "0")) || idx < 0 || idx >= MAX_FILES) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "No pending file transfers with that ID.");
        return;
    }

    struct FileTransfer *ft = get_file_transfer_struct_index(self->num, idx, FILE_TRANSFER_RECV);

    if (!ft) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "No pending file transfers with that ID.");
        return;
    }

    if (ft->state != FILE_TRANSFER_PENDING) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "No pending file transfers with that ID.");
        return;
    }

    if ((ft->file = fopen(ft->file_path, "a")) == NULL) {
        const char *msg =  "File transfer failed: Invalid download path.";
        close_file_transfer(self, m, ft, TOX_FILE_CONTROL_CANCEL, msg, notif_error);
        return;
    }

    Tox_Err_File_Control err;
    tox_file_control(m, self->num, ft->filenumber, TOX_FILE_CONTROL_RESUME, &err);

    if (err != TOX_ERR_FILE_CONTROL_OK) {
        goto on_recv_error;
    }

    line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Saving file [%ld] as: '%s'", idx, ft->file_path);

    /* prep progress bar line */
    char progline[MAX_STR_SIZE];
    init_progress_bar(progline);
    line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "%s", progline);

    ft->line_id = self->chatwin->hst->line_end->id + 2;
    ft->state = FILE_TRANSFER_STARTED;

    return;

on_recv_error:

    switch (err) {
        case TOX_ERR_FILE_CONTROL_FRIEND_NOT_FOUND:
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File transfer failed: Friend not found.");
            return;

        case TOX_ERR_FILE_CONTROL_FRIEND_NOT_CONNECTED:
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File transfer failed: Friend is not online.");
            return;

        case TOX_ERR_FILE_CONTROL_NOT_FOUND:
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File transfer failed: Invalid filenumber.");
            return;

        case TOX_ERR_FILE_CONTROL_SENDQ:
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File transfer failed: Connection error.");
            return;

        default:
            line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File transfer failed (error %d)\n", err);
            return;
    }
}

void cmd_sendfile(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    UNUSED_VAR(window);

    const char *errmsg = NULL;

    if (argc < 1) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File path required.");
        return;
    }

    char path[MAX_STR_SIZE];
    snprintf(path, sizeof(path), "%s", argv[1]);
    int path_len = strlen(path);

    if (path_len >= MAX_STR_SIZE) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File path exceeds character limit.");
        return;
    }

    FILE *file_to_send = fopen(path, "r");

    if (file_to_send == NULL) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "File not found.");
        return;
    }

    off_t filesize = file_size(path);

    if (filesize == 0) {
        line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid file.");
        fclose(file_to_send);
        return;
    }

    char file_name[TOX_MAX_FILENAME_LENGTH];
    size_t namelen = get_file_name(file_name, sizeof(file_name), path);

    Tox_Err_File_Send err;
    uint32_t filenum = tox_file_send(m, self->num, TOX_FILE_KIND_DATA, (uint64_t) filesize, NULL,
                                     (uint8_t *) file_name, namelen, &err);

    if (err != TOX_ERR_FILE_SEND_OK) {
        goto on_send_error;
    }

    struct FileTransfer *ft = new_file_transfer(self, self->num, filenum, FILE_TRANSFER_SEND, TOX_FILE_KIND_DATA);

    if (!ft) {
        err = TOX_ERR_FILE_SEND_TOO_MANY;
        goto on_send_error;
    }

    memcpy(ft->file_name, file_name, namelen + 1);
    ft->file = file_to_send;
    ft->file_size = filesize;
    tox_file_get_file_id(m, self->num, filenum, ft->file_id, NULL);

    char sizestr[32];
    bytes_convert_str(sizestr, sizeof(sizestr), filesize);
    line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "Sending file [%d]: '%s' (%s)", filenum, file_name, sizestr);

    return;

on_send_error:

    switch (err) {
        case TOX_ERR_FILE_SEND_FRIEND_NOT_FOUND:
            errmsg = "File transfer failed: Invalid friend.";
            break;

        case TOX_ERR_FILE_SEND_FRIEND_NOT_CONNECTED:
            errmsg = "File transfer failed: Friend is offline.";
            break;

        case TOX_ERR_FILE_SEND_NAME_TOO_LONG:
            errmsg = "File transfer failed: Filename is too long.";
            break;

        case TOX_ERR_FILE_SEND_TOO_MANY:
            errmsg = "File transfer failed: Too many concurrent file transfers.";
            break;

        default:
            errmsg = "File transfer failed.";
            break;
    }

    line_info_add(self, false, NULL, NULL, SYS_MSG, 0, 0, "%s", errmsg);
    tox_file_control(m, self->num, filenum, TOX_FILE_CONTROL_CANCEL, NULL);
    fclose(file_to_send);
}
