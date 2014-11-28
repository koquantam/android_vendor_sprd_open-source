#define LOG_TAG     "MODEMD"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cutils/properties.h>
#include <utils/Log.h>
#include <cutils/sockets.h>
#include "modemd.h"
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "modemd_ext.h"

int tl_modem_state = MODEM_READY;
int lf_modem_state = MODEM_READY;
int td_modem_state = MODEM_READY;
int lte_modem_state = MODEM_READY;
static int w_modem_state = MODEM_READY;
pthread_mutex_t tl_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lf_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t td_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lte_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t w_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t tl_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t lf_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t td_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t lte_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t w_cond = PTHREAD_COND_INITIALIZER;
static int epollfd = -1;
static int wakealarm_fd = -1;
/******************************************************
 *
 ** sipc interface begin
 *
 *****************************************************/

int loop_info_sockclients(const char* buf, const int len)
{
    int i, ret;

    /* info socket clients that modem is assert/hangup/blocked */
    for(i = 0; i < MAX_CLIENT_NUM; i++) {
        MODEMD_LOGD("client_fd[%d]=%d\n",i, client_fd[i]);
        if(client_fd[i] >= 0) {
            ret = write(client_fd[i], buf, len);
            MODEMD_LOGD("write %d bytes to client_fd[%d]:%d",
                    len, i, client_fd[i]);
            if(ret < 0) {
                MODEMD_LOGE("reset client_fd[%d]=-1",i);
                close(client_fd[i]);
                client_fd[i] = -1;
            }
        }
    }

    return 0;
}

static int load_sipc_image(char *fin, int offsetin, char *fout, int offsetout, int size)
{
    int res = -1, fdin, fdout, bufsize, i, rsize, rrsize, wsize;
    char buf[8192];
    int buf_size = sizeof(buf);

    MODEMD_LOGD("Loading %s in bank %s:%d %d", fin, fout, offsetout, size);

    fdin = open(fin, O_RDONLY, 0);
    fdout = open(fout, O_RDWR, 0);
    if (fdin < 0) {
        if (fdout>=0)
            close(fdout);

        MODEMD_LOGE("failed to open %s", fin);
        return -1;
    }
    if (fdout < 0) {
        close(fdin);
        MODEMD_LOGE("failed to open %s, error: %s", fout, strerror(errno));
        return -1;
    }

    if (lseek(fdin, offsetin, SEEK_SET) != offsetin) {
        MODEMD_LOGE("failed to lseek %d in %s", offsetin, fin);
        goto leave;
    }

    if (lseek(fdout, offsetout, SEEK_SET) != offsetout) {
        MODEMD_LOGE("failed to lseek %d in %s", offsetout, fout);
        goto leave;
    }

    for (i = 0; size > 0; i += min(size, buf_size)) {
        rsize = min(size, buf_size);
        rrsize = read(fdin, buf, rsize);
        if (rrsize != rsize) {
            MODEMD_LOGE("failed to read %s", fin);
            goto leave;
        }
        wsize = write(fdout, buf, rsize);
        if (wsize != rsize) {
            MODEMD_LOGE("failed to write %s [wsize = %d  rsize = %d  remain = %d]",
                    fout, wsize, rsize, size);
            goto leave;
        }
        size -= rsize;
    }

    res = 0;
leave:
    close(fdin);
    close(fdout);
    return res;
}
char path[128];
char persist_prop[128];
char path_char[128];
char proc_prop[128];
char modem_bank[128];
char dsp_bank[128];
char modem_stop[128];
char modem_start[128];


static int load_sipc_modem_img(int modem, int is_modem_assert)
{
    char modem_partition[128] = {0};
    char dsp_partition[128] = {0};
    int sipc_modem_size = 0;
    int sipc_dsp_size = 0;
    char alive_info[20]={0};
    int i, ret;
    memset(path, 0, sizeof(path));
    if ( -1 == property_get("ro.product.partitionpath", path, "") )
    {
        MODEMD_LOGD("%s: get partitionpath fail",__func__);
        return -1;
    }

   strcpy(persist_prop,PERSIST_MODEM_CHAR);
   system("echo 1 >/sys/power/wake_lock");

    if(modem == TD_MODEM) {
        sipc_modem_size = TD_MODEM_SIZE;
        sipc_dsp_size = TD_DSP_SIZE;
        strcat(persist_prop, "t.nvp");
        property_get(TD_PROC_PROP, proc_prop, "");
        /* write 1 to stop*/
        strcpy(modem_stop, proc_prop);
        strcat(modem_stop, MODEM_STOP);
        write_proc_file(modem_stop, 0, "1");

        strcpy(modem_bank, proc_prop);
        strcat(modem_bank, MODEM_BANK);
        strcpy(dsp_bank, proc_prop);
        strcat(dsp_bank, DSP_BANK);
        strcpy(modem_start, proc_prop);
        strcat(modem_start, MODEM_START);

        property_get(persist_prop,path_char,"");
        if(0 == strlen(path_char)){
              system("echo 1 >/sys/power/wake_unlock");
              MODEMD_LOGD("invalid ro.modem.x.nvp path_char %s\n",path_char);
              return 0;
         }
        MODEMD_LOGD("modem path_char %s \n",path_char);
        strcat(path,path_char);
        strcpy(modem_partition,path);
        strcpy(dsp_partition,path);
        strcat(modem_partition,"modem");
        strcat(dsp_partition,"dsp");

        /* load modem */
        MODEMD_LOGD("load modem image from %s to %s, len=%d",
                modem_partition, modem_bank, sipc_modem_size);
        load_sipc_image(modem_partition, 0, modem_bank, 0, sipc_modem_size);
        /* load dsp */
        MODEMD_LOGD("load dsp image from %s to %s, len=%d",
                dsp_partition, dsp_bank, sipc_dsp_size);
        load_sipc_image(dsp_partition, 0, dsp_bank, 0, sipc_dsp_size);

    } else if(modem == W_MODEM) {
        sipc_modem_size = W_MODEM_SIZE;
        sipc_dsp_size = W_DSP_SIZE;
        strcat(persist_prop, "w.nvp");
        property_get(W_PROC_PROP, proc_prop, "");
        /* write 1 to stop*/
        strcpy(modem_stop, proc_prop);
        strcat(modem_stop, MODEM_STOP);
        write_proc_file(modem_stop, 0, "1");

        strcpy(modem_bank, proc_prop);
        strcat(modem_bank, MODEM_BANK);
        strcpy(dsp_bank, proc_prop);
        strcat(dsp_bank, DSP_BANK);
        strcpy(modem_start, proc_prop);
        strcat(modem_start, MODEM_START);

        property_get(persist_prop,path_char,"");
        if(0 == strlen(path_char)){
              system("echo 1 >/sys/power/wake_unlock");
              MODEMD_LOGD("invalid ro.modem.x.nvp path_char %s\n",path_char);
              return 0;
         }
        MODEMD_LOGD("modem path_char %s \n",path_char);
        strcat(path,path_char);
        strcpy(modem_partition,path);
        strcpy(dsp_partition,path);
        strcat(modem_partition,"modem");
        strcat(dsp_partition,"dsp");

        /* load modem */
        MODEMD_LOGD("load modem image from %s to %s, len=%d",
                modem_partition, modem_bank, sipc_modem_size);
        load_sipc_image(modem_partition, 0, modem_bank, 0, sipc_modem_size);

        /* load dsp */
        MODEMD_LOGD("load dsp image from %s to %s, len=%d",
                dsp_partition, dsp_bank, sipc_dsp_size);
        load_sipc_image(dsp_partition, 0, dsp_bank, 0, sipc_dsp_size);
    } else if(modem == LF_MODEM) {
        char tgdsp_bank[128];
        char ldsp_bank[128];
        uint sipc_ldsp_size = 0;
        uint sipc_tgdsp_size = 0;

        sipc_modem_size = LF_MODEM_SIZE;
        sipc_tgdsp_size = LF_TGDSP_SIZE;
        sipc_ldsp_size = LF_LDSP_SIZE;
        strcat(persist_prop, "lf.nvp");
        property_get(LF_PROC_PROP, proc_prop, "");
        /* write 1 to stop*/
        strcpy(modem_stop, proc_prop);
        strcat(modem_stop, MODEM_STOP);
        write_proc_file(modem_stop, 0, "1");
        strcpy(modem_bank, proc_prop);
        strcat(modem_bank, MODEM_BANK);

        /*handle tgdsp image*/
        strcpy(tgdsp_bank, proc_prop);
        strcat(tgdsp_bank, TGDSP_BANK);

        /*handle ldsp image*/
        strcpy(ldsp_bank, proc_prop);
        strcat(ldsp_bank, LDSP_BANK);

        strcpy(modem_start, proc_prop);
        strcat(modem_start, MODEM_START);

        property_get(persist_prop,path_char,"");
        if(0 == strlen(path_char)){
              system("echo 1 >/sys/power/wake_unlock");
              MODEMD_LOGD("invalid ro.modem.x.nvp path_char %s\n",path_char);
              return 0;
         }
        MODEMD_LOGD("modem path_char %s \n",path_char);
        strcat(path,path_char);
        strcpy(modem_partition,path);
        strcpy(dsp_partition,path);
        strcat(modem_partition,"modem");
        strcat(dsp_partition,"gdsp");

        /* load modem */
        MODEMD_LOGD("load modem image from %s to %s, len=%d",
                modem_partition, modem_bank, sipc_modem_size);
        load_sipc_image(modem_partition, 0, modem_bank, 0, sipc_modem_size);


        /* load dsp */
        MODEMD_LOGD("load dsp image from %s to %s, len=%d",
                dsp_partition, tgdsp_bank, sipc_tgdsp_size);
        load_sipc_image(dsp_partition, 0, tgdsp_bank, 0, sipc_tgdsp_size);

        strcpy(dsp_partition,path);
        strcat(dsp_partition,"ldsp");
        load_sipc_image(dsp_partition, 0, ldsp_bank, 0, sipc_ldsp_size);

    } else if(modem == TL_MODEM) {
        char tgdsp_bank[128];
        char ldsp_bank[128];
        uint sipc_ldsp_size = 0;
        uint sipc_tgdsp_size = 0;

        sipc_modem_size = TL_MODEM_SIZE;
        sipc_tgdsp_size = TL_TGDSP_SIZE;
        sipc_ldsp_size = TL_LDSP_SIZE;
        strcat(persist_prop, "tl.nvp");
        property_get(TL_PROC_PROP, proc_prop, "");
        /* write 1 to stop*/
        strcpy(modem_stop, proc_prop);
        strcat(modem_stop, MODEM_STOP);
        write_proc_file(modem_stop, 0, "1");
        strcpy(modem_bank, proc_prop);
        strcat(modem_bank, MODEM_BANK);

        /*handle tgdsp image*/
        strcpy(tgdsp_bank, proc_prop);
        strcat(tgdsp_bank, TGDSP_BANK);

        /*handle ldsp image*/
        strcpy(ldsp_bank, proc_prop);
        strcat(ldsp_bank, LDSP_BANK);

        strcpy(modem_start, proc_prop);
        strcat(modem_start, MODEM_START);

        property_get(persist_prop,path_char,"");
        if(0 == strlen(path_char)){
              system("echo 1 >/sys/power/wake_unlock");
              MODEMD_LOGD("invalid ro.modem.x.nvp path_char %s\n",path_char);
              return 0;
         }
        MODEMD_LOGD("modem path_char %s \n",path_char);
        strcat(path,path_char);
        strcpy(modem_partition,path);
        strcpy(dsp_partition,path);
        strcat(modem_partition,"modem");
        strcat(dsp_partition,"tgdsp");

        /* load modem */
        MODEMD_LOGD("load modem image from %s to %s, len=%d",
                modem_partition, modem_bank, sipc_modem_size);
        load_sipc_image(modem_partition, 0, modem_bank, 0, sipc_modem_size);


        /* load dsp */
        MODEMD_LOGD("load dsp image from %s to %s, len=%d",
                dsp_partition, tgdsp_bank, sipc_tgdsp_size);
        load_sipc_image(dsp_partition, 0, tgdsp_bank, 0, sipc_tgdsp_size);

        strcpy(dsp_partition,path);
        strcat(dsp_partition,"ldsp");
        load_sipc_image(dsp_partition, 0, ldsp_bank, 0, sipc_ldsp_size);
    }
    stop_service(modem, 0);
    
    /* write 1 to start*/
    write_proc_file(modem_start, 0, "1");
    
    if(modem == TD_MODEM) {
        strcpy(alive_info, "TD Modem Alive");
    } else if(modem == W_MODEM) {
        strcpy(alive_info, "W Modem Alive");
    } else if(modem == TL_MODEM) {
        strcpy(alive_info, "TL Modem Alive");
    } else if(modem == LF_MODEM) {
        strcpy(alive_info, "LF Modem Alive");
    } else {
        MODEMD_LOGE("error unkown modem  alive_info");
    }
    MODEMD_LOGD("wait for 20s\n");
      struct itimerspec itval;
      struct epoll_event events[1];
      struct epoll_event ev;
      int nevents;
      if (wakealarm_fd < 0)
          wakealarm_fd = timerfd_create(CLOCK_BOOTTIME_ALARM, TFD_NONBLOCK);
      if (wakealarm_fd == -1) {
          MODEMD_LOGE("wakealarm_init: timerfd_create failed\n");
          return -1;
      }
      if(epollfd < 0)
      {
          epollfd = epoll_create(1);
          if (epollfd == -1) {
              MODEMD_LOGE("healthd_mainloop: epoll_create failed; errno=%d\n", errno);
              return -1;
          }
          ev.events = EPOLLIN | EPOLLWAKEUP;
          if (epoll_ctl(epollfd, EPOLL_CTL_ADD, wakealarm_fd, &ev) == -1)
          {
              MODEMD_LOGD("load_sipc_modem_img: epoll_ctl for wakealarm_fd failed; errno=%d\n", errno);
              return -1;
          }
      }
      itval.it_interval.tv_sec = 20;
      itval.it_interval.tv_nsec = 0;
      itval.it_value.tv_sec = 20;
      itval.it_value.tv_nsec = 0;
      if (timerfd_settime(wakealarm_fd, 0, &itval, NULL) == -1){
          MODEMD_LOGE("load_sipc_modem_img: timerfd_settime failed\n");
      }
      do {
          nevents = epoll_wait(epollfd, events, 1, -1);
          if (nevents == -1) {
              if (errno == EINTR)
              continue;
              MODEMD_LOGE("load_sipc_modem_img: epoll_wait failed\n");
              break;
          }
         break ;
      }while(1) ;
    // sleep(20);
    if(is_modem_assert) {
        /* info socket clients that modem is reset */
        MODEMD_LOGD("Info all the sock clients that modem is alive");
        loop_info_sockclients(alive_info, strlen(alive_info)+1);
    }

    start_service(modem, 0, 1);
    if(modem == TD_MODEM) {
            pthread_mutex_lock(&td_state_mutex);
            td_modem_state = MODEM_READY;
            pthread_cond_signal(&td_cond);
            pthread_mutex_unlock(&td_state_mutex);
    } else if(modem == W_MODEM) {
            pthread_mutex_lock(&w_state_mutex);
            w_modem_state = MODEM_READY;
            pthread_cond_signal(&w_cond);
            pthread_mutex_unlock(&w_state_mutex);
    } else if(modem == LF_MODEM) {
            pthread_mutex_lock(&lf_state_mutex);
            lf_modem_state = MODEM_READY;
            pthread_cond_signal(&lf_cond);
            pthread_mutex_unlock(&lf_state_mutex);
    } else if(modem == TL_MODEM) {
            pthread_mutex_lock(&tl_state_mutex);
            tl_modem_state = MODEM_READY;
            pthread_cond_signal(&tl_cond);
            pthread_mutex_unlock(&tl_state_mutex);
     }
    system("echo 1 >/sys/power/wake_unlock");
    return 0;
}

void *detect_modem_blocked(void *par)
{
    int soc_fd, i, ret;
    int modem, numRead, loop_fd;
    char socket_name[256] = {0}, loop_dev[256] = {0};
    char prop[256], buf[256], buffer[256];
    int is_reset, is_assert = 0;

    if(par == NULL){
                MODEMD_LOGE("%s: input parameter is NULL!", __func__);
        return NULL;
        }

    modem = *((int *)par);
    if(modem == TD_MODEM) {
        strcpy(socket_name, PHSTD_SOCKET_NAME);
    } else if(modem == W_MODEM) {
        strcpy(socket_name, PHSW_SOCKET_NAME);
    } else if (modem == LTE_MODEM) {
        strcpy(socket_name, PHSLTE_SOCKET_NAME);
    } else if(modem == TL_MODEM) {
        strcpy(socket_name, PHSTL_SOCKET_NAME);
    } else if (modem == LF_MODEM) {
        strcpy(socket_name, PHSLF_SOCKET_NAME);
    } else {
        MODEMD_LOGE("%s: input wrong modem type!", __func__);
        return NULL;
    }

reconnect:
    MODEMD_LOGD("%s: try to connect socket %s...", __func__, socket_name);
    soc_fd = socket_local_client(socket_name,
        ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

    while(soc_fd < 0) {
        usleep(10*1000);
        soc_fd = socket_local_client(socket_name,
            ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    }
    MODEMD_LOGD("%s: connect socket %s success", __func__, socket_name);

    for(;;) {
        memset(buf, 0, sizeof(buf));
        MODEMD_LOGD("%s: begin detect blocked event on socket %s...", __func__, socket_name);
        do {
            numRead = read(soc_fd, buf, sizeof(buf));
        } while(numRead < 0 && errno == EINTR);
        if(numRead <= 0) {
            MODEMD_LOGE("%s: read numRead=%d, error: %s", __func__, numRead, strerror(errno));
            if(modem == TD_MODEM) {
                pthread_mutex_lock(&td_state_mutex);
                if(td_modem_state != MODEM_READY) {
                    MODEMD_LOGD("%s: wait for TD  modem ready ...", __func__);
                    pthread_cond_wait(&td_cond, &td_state_mutex);
                    MODEMD_LOGD("%s: TD modem ready, wake up", __func__);
                }
                pthread_mutex_unlock(&td_state_mutex);
            } else if(modem == W_MODEM) {
                pthread_mutex_lock(&w_state_mutex);
                if(w_modem_state != MODEM_READY) {
                    MODEMD_LOGD("%s: wait for W modem ready ...", __func__);
                    pthread_cond_wait(&w_cond, &w_state_mutex);
                    MODEMD_LOGD("%s: W modem ready, wake up", __func__);
                }
                pthread_mutex_unlock(&w_state_mutex);
            } else if(modem == LTE_MODEM) {
                pthread_mutex_lock(&lte_state_mutex);
                if(lte_modem_state != MODEM_READY) {
                    MODEMD_LOGD("%s: wait for LTE modem ready ...", __func__);
                    pthread_cond_wait(&lte_cond, &lte_state_mutex);
                    MODEMD_LOGD("%s: LTE modem ready, wake up", __func__);
                }
                pthread_mutex_unlock(&lte_state_mutex);
            } else if(modem == TL_MODEM) {
                pthread_mutex_lock(&tl_state_mutex);
                if(tl_modem_state != MODEM_READY) {
                    MODEMD_LOGD("%s: wait for modem ready ...", __func__);
                    pthread_cond_wait(&tl_cond, &tl_state_mutex);
                    MODEMD_LOGD("%s: modem ready, wake up", __func__);
                }
                pthread_mutex_unlock(&tl_state_mutex);
            } else if(modem == LF_MODEM) {
                pthread_mutex_lock(&lf_state_mutex);
                if(lf_modem_state != MODEM_READY) {
                    MODEMD_LOGD("%s: wait for modem ready ...", __func__);
                    pthread_cond_wait(&lf_cond, &lf_state_mutex);
                    MODEMD_LOGD("%s: modem ready, wake up", __func__);
                }
                pthread_mutex_unlock(&lf_state_mutex);
            }
            close(soc_fd);
            goto reconnect;
        }
        MODEMD_LOGD("%s: read numRead=%d, buf=%s", __func__, numRead, buf);

        if(strstr(buf, "TD Modem Blocked") != NULL) {
            pthread_mutex_lock(&td_state_mutex);
            if(td_modem_state != MODEM_READY) {
                pthread_mutex_unlock(&td_state_mutex);
                continue;
            }
            td_modem_state = MODEM_ASSERT;
            pthread_mutex_unlock(&td_state_mutex);
            if (is_external_modem()) {
                goto raw_reset;
            }
        } else if(strstr(buf, "W Modem Blocked") != NULL) {
            pthread_mutex_lock(&w_state_mutex);
            if(w_modem_state != MODEM_READY) {
                pthread_mutex_unlock(&w_state_mutex);
                continue;
            }
            w_modem_state = MODEM_ASSERT;
            pthread_mutex_unlock(&w_state_mutex);
        } else if(strstr(buf, "LTE Modem Blocked") != NULL) {
            pthread_mutex_lock(&lte_state_mutex);
            if(lte_modem_state != MODEM_READY) {
                pthread_mutex_unlock(&lte_state_mutex);
                continue;
            }
            lte_modem_state = MODEM_ASSERT;
            pthread_mutex_unlock(&lte_state_mutex);
            if (is_external_modem()) {
                goto raw_reset;
            }
        } else if(strstr(buf, "TL Modem Blocked") != NULL) {
            pthread_mutex_lock(&tl_state_mutex);
            if(tl_modem_state != MODEM_READY) {
                pthread_mutex_unlock(&tl_state_mutex);
                continue;
            }
            tl_modem_state = MODEM_ASSERT;
            pthread_mutex_unlock(&tl_state_mutex);
            if (is_external_modem()) {
                goto raw_reset;
            }
        }else if(strstr(buf, "LF Modem Blocked") != NULL) {
            pthread_mutex_lock(&lf_state_mutex);
            if(lf_modem_state != MODEM_READY) {
                pthread_mutex_unlock(&lf_state_mutex);
                continue;
            }
            lf_modem_state = MODEM_ASSERT;
            pthread_mutex_unlock(&lf_state_mutex);
            if (is_external_modem()) {
                goto raw_reset;
            }
        } else {
            MODEMD_LOGD("%s: read invalid string from socket %s", __func__, socket_name);
            continue;
        }

        is_assert = 1;

        if(modem == TD_MODEM) {
            property_get(TD_LOOP_PROP, loop_dev, DEFAULT_TD_LOOP_DEV);
        } else if(modem == W_MODEM) {
            property_get(W_LOOP_PROP, loop_dev, DEFAULT_W_LOOP_DEV);
        } else if(modem == TL_MODEM) {
            property_get(TL_LOOP_PROP, loop_dev, DEFAULT_TL_LOOP_DEV);
        } else if(modem == LF_MODEM) {
            property_get(LF_LOOP_PROP, loop_dev, DEFAULT_LF_LOOP_DEV);
        } else if(modem == LTE_MODEM) {
            property_get(L_LOOP_PROP, loop_dev, DEFAULT_L_LOOP_DEV);
        } else {
            MODEMD_LOGE("%s: invalid modem type, exit", __func__);
            return NULL;
        }

        loop_fd = open(loop_dev, O_RDWR | O_NONBLOCK);
        MODEMD_LOGD("%s: open loop dev: %s, fd = %d", __func__, loop_dev, loop_fd);
        if (loop_fd < 0) {
            MODEMD_LOGE("%s: open %s failed, error: %s", __func__, loop_dev, strerror(errno));
            goto raw_reset;
        }
        ret = write(loop_fd, "AT", 3);
        if(ret < 0) {
            MODEMD_LOGE("%s: write %s failed, error:%s", __func__, loop_dev, strerror(errno));
            close(loop_fd);
            goto raw_reset;
        }
        usleep(100*1000);
        do {
            ret = read(loop_fd, buffer, sizeof(buffer));
        } while(ret < 0 && errno == EINTR);
        if (ret <= 0) {
            MODEMD_LOGE("%s: read %d return %d, errno = %s", __func__, loop_fd , ret, strerror(errno));
            close(loop_fd);
            goto raw_reset;
        }
        if(!strcmp(buffer, "AT")) {
            MODEMD_LOGD("%s: loop spipe %s is OK", __func__, loop_dev);
        }
        close(loop_fd);

raw_reset:
        /* info socket clients that modem is blocked */
        MODEMD_LOGE("Info all the sock clients that modem is blocked");
        loop_info_sockclients(buf, numRead);

        /* reset or not according to property */
        memset(prop, 0, sizeof(prop));
        property_get(MODEM_RESET_PROP, prop, "0");
        is_reset = atoi(prop);
        if(is_reset) {
            MODEMD_LOGD("%s: reset is enabled, reload modem...", __func__);
            if (is_external_modem()) {
               get_ext_modem_if()->load_modem_image();
            } else {
               load_sipc_modem_img(modem, is_assert);
               is_assert = 0;
           }
        } else {
            MODEMD_LOGD("%s: reset is not enabled , not reset", __func__);
        }
    }
    close(soc_fd);
    return (void*) NULL;
}

/* loop detect sipc modem state */
void* detect_sipc_modem(void *param)
{
    char assert_dev[256] = {0};
    char watchdog_dev[256] = {0};
    int i, ret, assert_fd, watchdog_fd, max_fd, fd = -1;
    fd_set rfds;
    int is_reset, modem = -1;
    char buf[256], prop[256];
    int numRead;
    int is_assert = 0;

    if(param != NULL)
        modem = *((int *)param);

    if(modem == TD_MODEM) {
        property_get(TD_ASSERT_PROP, assert_dev, DEFAULT_TD_ASSERT_DEV);
        snprintf(watchdog_dev, sizeof(watchdog_dev), "%s", TD_WATCHDOG_DEV);
    } else if(modem == W_MODEM) {
        property_get(W_ASSERT_PROP, assert_dev, DEFAULT_W_ASSERT_DEV);
        snprintf(watchdog_dev, sizeof(watchdog_dev), "%s", W_WATCHDOG_DEV);
    } else if(modem == TL_MODEM) {
        property_get(TL_ASSERT_PROP, assert_dev, DEFAULT_TL_ASSERT_DEV);
        snprintf(watchdog_dev, sizeof(watchdog_dev), "%s", TL_WATCHDOG_DEV);
    } else if(modem == LF_MODEM) {
        property_get(LF_ASSERT_PROP, assert_dev, DEFAULT_LF_ASSERT_DEV);
        snprintf(watchdog_dev, sizeof(watchdog_dev), "%s", LF_WATCHDOG_DEV);
    } else if(modem == LTE_MODEM) {
        property_get(L_ASSERT_PROP, assert_dev, DEFAULT_L_ASSERT_DEV);
        snprintf(watchdog_dev, sizeof(watchdog_dev), "%s", L_WATCHDOG_DEV);
    } else {
        MODEMD_LOGE("%s: input wrong modem type!", __func__);
                return NULL;
        }

    assert_fd = open(assert_dev, O_RDWR);
    MODEMD_LOGD("%s: open assert dev: %s, fd = %d", __func__, assert_dev, assert_fd);
    if (assert_fd < 0) {
        MODEMD_LOGE("open %s failed, error: %s", assert_dev, strerror(errno));
        return NULL;
    }

    watchdog_fd = open(watchdog_dev, O_RDONLY);
    MODEMD_LOGD("%s: open watchdog dev: %s, fd = %d", __func__, watchdog_dev, watchdog_fd);
    if (watchdog_fd < 0) {
        close(assert_fd);
        MODEMD_LOGE("open %s failed, error: %s", watchdog_dev, strerror(errno));
        return NULL;
    }

    max_fd = watchdog_fd > assert_fd ? watchdog_fd : assert_fd;

    FD_ZERO(&rfds);
    FD_SET(assert_fd, &rfds);
    FD_SET(watchdog_fd, &rfds);

    for (;;) {
        MODEMD_LOGD("%s: wait for modem assert/hangup event ...", __func__);
        do {
            ret = select(max_fd + 1, &rfds, NULL, NULL, 0);
        } while(ret == -1 && errno == EINTR);
        if (ret > 0) {
            if (FD_ISSET(assert_fd, &rfds)) {
                fd = assert_fd;
            } else if (FD_ISSET(watchdog_fd, &rfds)) {
                fd = watchdog_fd;
            }
            if(fd <= 0) {
                MODEMD_LOGE("none of assert and watchdog fd is readalbe");
                sleep(1);
                continue;
            }
            memset(buf, 0, sizeof(buf));
            numRead = read(fd, buf, sizeof(buf));
            if (numRead <= 0) {
                MODEMD_LOGE("read %d return %d, errno = %s", fd , numRead, strerror(errno));
                sleep(1);
                continue;
            }

            MODEMD_LOGD("buf=%s", buf);
            if(strstr(buf, "Modem Reset")) {
                MODEMD_LOGD("modem reset happen, reload modem...");
                if(modem == TD_MODEM) {
                    pthread_mutex_lock(&td_state_mutex);
                    td_modem_state = MODEM_RESET;
                    pthread_mutex_unlock(&td_state_mutex);
                } else if(modem == W_MODEM) {
                    pthread_mutex_lock(&w_state_mutex);
                    w_modem_state = MODEM_RESET;
                    pthread_mutex_unlock(&w_state_mutex);
                } else if(modem == TL_MODEM) {
                    pthread_mutex_lock(&tl_state_mutex);
                    tl_modem_state = MODEM_RESET;
                    pthread_mutex_unlock(&tl_state_mutex);
                } else if(modem == LF_MODEM) {
                    pthread_mutex_lock(&lf_state_mutex);
                    lf_modem_state = MODEM_RESET;
                    pthread_mutex_unlock(&lf_state_mutex);
                }

                /* info socket clients that modem is reset */
                MODEMD_LOGE("Info all the sock clients that modem is reset");
                loop_info_sockclients(buf, numRead);
                is_assert = 1;
                load_sipc_modem_img(modem, is_assert);
                is_assert = 0;
            } else if(strstr(buf, "Modem Assert") || strstr(buf, "wdtirq")) {
                if(modem == TD_MODEM) {
                    pthread_mutex_lock(&td_state_mutex);
                    td_modem_state = MODEM_ASSERT;
                    pthread_mutex_unlock(&td_state_mutex);
                } else if(modem == W_MODEM) {
                    pthread_mutex_lock(&w_state_mutex);
                    w_modem_state = MODEM_ASSERT;
                    pthread_mutex_unlock(&w_state_mutex);
                } else if(modem == TL_MODEM) {
                    pthread_mutex_lock(&tl_state_mutex);
                    tl_modem_state = MODEM_ASSERT;
                    pthread_mutex_unlock(&tl_state_mutex);
                } else if(modem == LF_MODEM) {
                    pthread_mutex_lock(&lf_state_mutex);
                    lf_modem_state = MODEM_ASSERT;
                    pthread_mutex_unlock(&lf_state_mutex);
                }
                if(strstr(buf, "wdtirq")) {
                    if(modem == TD_MODEM) {
                        MODEMD_LOGD("td modem hangup");
                        strcpy(buf, "TD Modem Hang");
                        numRead = sizeof("TD Modem Hang");
                    } else if(modem == W_MODEM) {
                        MODEMD_LOGD("w modem hangup");
                        strcpy(buf, "W Modem Hang");
                        numRead = sizeof("W Modem Hang");
                    } else if(modem == TL_MODEM) {
                        MODEMD_LOGD("tl modem hangup");
                        strcpy(buf, "TL Modem Hang");
                        numRead = sizeof("TL Modem Hang");
                    } else if(modem == LF_MODEM) {
                        MODEMD_LOGD("lf modem hangup");
                        strcpy(buf, "LF Modem Hang");
                        numRead = sizeof("LF Modem Hang");
                    }
                } else {
                    MODEMD_LOGD("modem assert happen");
                }
                is_assert = 1;

                /* info socket clients that modem is assert or hangup */
                MODEMD_LOGE("Info all the sock clients that modem is assert/hangup");
                loop_info_sockclients(buf, numRead);

                /* reset or not according to property */
                memset(prop, 0, sizeof(prop));
                property_get(MODEM_RESET_PROP, prop, "0");
                is_reset = atoi(prop);
                if(is_reset) {
                    MODEMD_LOGD("modem reset is enabled, reload modem...");
                    load_sipc_modem_img(modem, is_assert);
                    is_assert = 0;
                } else {
                    MODEMD_LOGD("modem reset is not enabled , will not reset");
                }
            }
        }
    }
}
/******************************************************
 *
 ** sipc interface end
 *
 *****************************************************/

