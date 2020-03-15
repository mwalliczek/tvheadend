/*
 *  Tvheadend - DAB Input System
 *
 *  Copyright (C) 2019 Matthias Walliczek
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TVH_DAB_H__
#define __TVH_DAB_H__

#ifndef __TVH_INPUT_H__
#error "Use header file input.h not input/dab.h"
#endif

/* Types */
typedef struct dab_network       dab_network_t;
typedef struct dab_service       dab_service_t;
typedef struct dab_ensemble      dab_ensemble_t;
typedef struct dab_ensemble_instance      dab_ensemble_instance_t;
typedef struct dab_input         dab_input_t;
typedef struct dab_network_link  dab_network_link_t;

/* Lists */
typedef LIST_HEAD (,dab_network)             dab_network_list_t;
typedef LIST_HEAD (,dab_input)               dab_input_list_t;
typedef TAILQ_HEAD(dab_ensemble_queue,dab_ensemble) dab_ensemble_queue_t;
typedef LIST_HEAD (,dab_ensemble)                 dab_ensemble_list_t;
typedef LIST_HEAD (,dab_network_link)        dab_network_link_list_t;

/* Classes */
extern const idclass_t dab_network_class;
extern const idclass_t dab_ensemble_class;
extern const idclass_t dab_ensemble_instance_class;
extern const idclass_t dab_service_class;
extern const idclass_t dab_service_raw_class;
extern const idclass_t dab_input_class;

/* **************************************************************************
 * Setup / Tear down
 * *************************************************************************/

void dab_init ( void );
void dab_done ( void );

/* **************************************************************************
 * Logical network
 * *************************************************************************/

/* Network/Input linkage */
struct dab_network_link
{
  int                             mnl_mark;
  dab_input_t                  *mnl_input;
  dab_network_t                *mnl_network;
  LIST_ENTRY(dab_network_link) mnl_mn_link;
  LIST_ENTRY(dab_network_link) mnl_mi_link;
};


/* Network */
struct dab_network
{
  idnode_t                   mn_id;
  LIST_ENTRY(dab_network) mn_global_link;

  /*
   * Identification
   */
  char                    *mn_network_name;
  int                      mn_wizard;
  uint8_t                  mn_wizard_free;

  /*
   * Inputs
   */
  dab_network_link_list_t   mn_inputs;

  /*
   * Multiplexes
   */
  dab_ensemble_list_t       mn_ensembles;

  /*
   * Scanning
   */
  dab_ensemble_queue_t mn_scan_pend;    // Pending muxes
  dab_ensemble_queue_t mn_scan_ipend;   // Pending muxes (idle)
  dab_ensemble_queue_t mn_scan_active;  // Active muxes
  mtimer_t           mn_scan_timer;   // Timer for activity

  /*
   * Functions
   */
  void              (*mn_delete)       (dab_network_t*, int delconf);
  void              (*mn_display_name) (dab_network_t*, char *buf, size_t len);
  htsmsg_t *        (*mn_config_save)  (dab_network_t*, char *filename, size_t fsize);
  dab_ensemble_t*     (*mn_create_ensemble)
    (dab_network_t*, void *origin, uint32_t onid, void *conf, int force);
  dab_service_t* (*mn_create_service)
    (dab_ensemble_t*, uint16_t sid);
  const idclass_t*  (*mn_ensemble_class)   (dab_network_t*);
  dab_ensemble_t *    (*mn_ensemble_create2) (dab_network_t *mn, htsmsg_t *conf);
  void              (*mn_scan)        (dab_network_t*);

  /*
   * Configuration
   */
  int      mn_enabled;
  int      mn_autodiscovery;
  int      mn_skipinitscan;
  int      mn_idlescan;
  int      mn_localtime;
};

typedef struct subchannelmap subChannel;

//      The service component describes the actual service
//      It really should be a union
struct servicecomponents {
                int         inUse;          // just administration
   int32_t       TMid;           // the transport mode
   int32_t      componentNr;    // component

   int32_t      ASCTy;          // used for audio
   int32_t      PS_flag;        // use for both audio and packet
   int32_t      subchannelId;   // used in both audio and packet
   uint32_t     SCId;           // used in packet
   uint32_t     CAflag;         // used in packet (or not at all)
   int32_t      DSCTy;          // used in packet
   uint32_t	DGflag;		// used for TDC
   int32_t      packetAddress;  // used in packet
   int32_t	appType;	// used in packet
   int		is_madePublic;
};

typedef struct servicecomponents serviceComponent;

struct subchannelmap {
   int		inUse;
   int32_t	SubChId;
   int32_t	StartAddr;
   int32_t	Length;
   int32_t	shortForm;
   int32_t	protLevel;
   int32_t	BitRate;
   int32_t	language;
   int32_t	FEC_scheme;
};

struct dab_ensemble
{
        idnode_t 	 mm_id;
        int      	 mm_refcount;
        
        /*
         * Identification
         */
  
        LIST_ENTRY(dab_ensemble)  mm_network_link;
        dab_network_t	*mm_network;
        char    	*mm_nicename;
        char            *mm_provider_network_name;
        uint32_t         mm_onid;
        int64_t          mm_start_monoclock;
        time_t   	 mm_created;
        uint32_t         mm_freq;

        /*
         * Services
         */
  
        LIST_HEAD(,dab_service) mm_services;

        /*
         * Scanning
         */

        time_t		 mm_scan_first;   ///< Time for the first successful scan
        time_t		 mm_scan_last_seen; ///< Time for the last successful scan

        mpegts_mux_scan_result_t mm_scan_result;  ///< Result of last scan
        int                      mm_scan_weight;  ///< Scan priority
        int                      mm_scan_flags;   ///< Subscription flags
        int                      mm_scan_init;    ///< Flag to timeout handler
        mtimer_t                 mm_scan_timeout; ///< Timer to handle timeout
        TAILQ_ENTRY(dab_ensemble)  mm_scan_link;    ///< Link to Queue
        mpegts_mux_scan_state_t  mm_scan_state;   ///< Scanning state

        /*
         * Physical instances
         */

        LIST_HEAD(, dab_ensemble_instance) mm_instances;
        dab_ensemble_instance_t      *mm_active;
        LIST_HEAD(,service)         mm_transports;

        /*
         * Raw subscriptions
         */

        LIST_HEAD(, th_subscription) mm_raw_subs;

        /*
         * Data processing
         */
        tvh_mutex_t                 mm_tables_lock;
        subChannel	subChannels[64];
        serviceComponent	ServiceComps[64];

        /*
         * Functions
         */

        void (*mm_delete)           (dab_ensemble_t *mm, int delconf);
        void (*mm_free)             (dab_ensemble_t *mm);
        htsmsg_t *(*mm_config_save) (dab_ensemble_t *mm, char *filename, size_t fsize);
        void (*mm_display_name)     (dab_ensemble_t*, char *buf, size_t len);
        int  (*mm_is_enabled)       (dab_ensemble_t *mm);
        void (*mm_stop)             (dab_ensemble_t *mm, int force, int reason);
        void (*mm_create_instances) (dab_ensemble_t*);

        /*
         * Configuration
         */
        int      mm_enabled;
};

struct dab_service
{
	service_t; // Parent

	int      s_dab_subscription_flags;
	int      s_dab_subscription_weight;

	char    *s_dab_svcname;
	char    *s_dab_provider;
	time_t   s_dab_created;
	time_t   s_dab_last_seen;
	time_t   s_dab_check_seen;
	
	/*
	 * Link to carrying ensemble and active adapter
	 */
	LIST_ENTRY(dab_service) s_dab_ensemble_link;
	dab_ensemble_t          *s_dab_ensemble;
	dab_input_t       	*s_dab_active_input;
	
	/**
	 * When a subscription request SMT_DAB, chunk them together
	 * in order to reduce load.
	 */
	sbuf_t s_tsbuf;
	int64_t s_tsbuf_last;

        int32_t       TMid;           // the transport mode
	int32_t      ASCTy;          // used for audio
	int32_t      PS_flag;        // use for both audio and packet
        int32_t      subChId;
        int32_t      hasLanguage;
        int32_t language;
        int32_t programType;
        int32_t FEC_scheme;
};

/* **************************************************************************
 * Physical Network
 * *************************************************************************/

/* Physical ensemble instance */
struct dab_ensemble_instance
{
	tvh_input_instance_t;

	LIST_ENTRY(dab_ensemble_instance) mmi_ensemble_link;
	LIST_ENTRY(dab_ensemble_instance) mmi_active_link;

	dab_ensemble_t   *mmi_ensemble;
	dab_input_t *mmi_input;
	
	int             mmi_start_weight;
        int             mmi_tune_failed;
        int		fibProcessorIsSynced;
};

/* Input source */
struct dab_input
{
  tvh_input_t;

  int mi_enabled;

  int mi_instance;

  char *mi_name;

  int mi_priority;
  int mi_streaming_priority;

  int mi_ota_epg;

  int mi_initscan;
  int mi_idlescan;
  uint32_t mi_free_weight;

  char *mi_linked;

  LIST_ENTRY(dab_input) mi_global_link;

  dab_network_link_list_t mi_networks;

  LIST_HEAD(,tvh_input_instance) mi_ensemble_instances;

  /*
   * Status
   */
  mtimer_t mi_status_timer;

  /*
   * Input processing
   */

  int mi_running;            /* threads running */
  int64_t mi_last_dispatch;

  /* Data processing/output */
  // Note: this lock (mi_output_lock) protects all the remaining
  //       data fields (excluding the callback functions)
  tvh_mutex_t                     mi_output_lock;

  /* Active sources */
  LIST_HEAD(,dab_ensemble_instance) mi_ensemble_active;

  /* DBus */
#if ENABLE_DBUS_1
  int64_t                         mi_dbus_subs;
#endif

  /*
   * Functions
   */
  int  (*mi_is_enabled)     (dab_input_t*, dab_ensemble_t *mm, int flags, int weight);
  void (*mi_enabled_updated)(dab_input_t*);
  void (*mi_display_name)   (dab_input_t*, char *buf, size_t len);
  int  (*mi_get_weight)     (dab_input_t*, dab_ensemble_t *mm, int flags, int weight);
  int  (*mi_get_priority)   (dab_input_t*, dab_ensemble_t *mm, int flags);
  int  (*mi_get_grace)      (dab_input_t*, dab_ensemble_t *mm);
  int  (*mi_warm_ensemble)       (dab_input_t*,dab_ensemble_instance_t*);
  int  (*mi_start_ensemble)      (dab_input_t*,dab_ensemble_instance_t*, int weight);
  void (*mi_stop_ensemble)       (dab_input_t*,dab_ensemble_instance_t*);
  void (*mi_open_service)   (dab_input_t*,dab_service_t*, int flags, int first, int weight);
  void (*mi_close_service)  (dab_input_t*,dab_service_t*);
  void (*mi_create_ensemble_instance) (dab_input_t*,dab_ensemble_t*);
  void (*mi_started_ensemble)    (dab_input_t*,dab_ensemble_instance_t*);
  void (*mi_stopping_ensemble)   (dab_input_t*,dab_ensemble_instance_t*);
  void (*mi_stopped_ensemble)    (dab_input_t*,dab_ensemble_instance_t*);
  int  (*mi_has_subscription) (dab_input_t*, dab_ensemble_t *mm);
  void (*mi_error)          (dab_input_t*, dab_ensemble_t *, int tss_flags);
  void (*mi_empty_status)   (dab_input_t*, tvh_input_stream_t *);
  idnode_set_t *(*mi_network_list) (dab_input_t*);
};

/* ****************************************************************************
 * Lists
 * ***************************************************************************/

extern dab_network_list_t dab_network_all;

typedef struct dab_network_builder {
  LIST_ENTRY(dab_network_builder) link;
  const idclass_t     *idc;
  dab_network_t * (*build) ( const idclass_t *idc, htsmsg_t *conf );
} dab_network_builder_t;


typedef LIST_HEAD(,dab_network_builder) dab_network_builder_list_t;

extern dab_network_builder_list_t dab_network_builders;

/* ****************************************************************************
 * Functions
 * ***************************************************************************/

dab_input_t *dab_input_create0
  ( dab_input_t *mi, const idclass_t *idc, const char *uuid, htsmsg_t *c );

#define dab_input_create(t, u, c)\
  (struct t*)dab_input_create0(calloc(1, sizeof(struct t)), &t##_class, u, c)

#define dab_input_create1(u, c)\
  dab_input_create0(calloc(1, sizeof(dab_input_t)),\
                       &dab_input_class, u, c)

void dab_input_stop_all ( dab_input_t *mi );

void dab_input_delete ( dab_input_t *mi, int delconf );

static inline dab_input_t *dab_input_find(const char *uuid)
  { return idnode_find(uuid, &dab_input_class, NULL); }

int dab_input_set_networks ( dab_input_t *mi, htsmsg_t *msg );

int dab_input_add_network  ( dab_input_t *mi, dab_network_t *mn );

void dab_input_open_service ( dab_input_t *mi, dab_service_t *s, int flags, int init, int weight );
void dab_input_close_service ( dab_input_t *mi, dab_service_t *s );

void dab_input_status_timer ( void *p );

int dab_input_grace ( dab_input_t * mi, dab_ensemble_t * mm );

int
dab_input_get_weight(dab_input_t *mi, dab_ensemble_t *mm, int flags, int weight );

int
dab_input_get_priority(dab_input_t *mi, dab_ensemble_t *mm, int flags );

int
dab_input_get_grace(dab_input_t *mi, dab_ensemble_t *mm);

void dab_input_create_ensemble_instance ( dab_input_t *mi, dab_ensemble_t *mm );

int dab_input_warm_ensemble ( dab_input_t *mi, dab_ensemble_instance_t *mmi );

void dab_input_save ( dab_input_t *mi, htsmsg_t *c );

int dab_input_is_enabled(dab_input_t * mi, dab_ensemble_t *mm, int flags, int weight );

void dab_input_set_enabled ( dab_input_t *mi, int enabled );

void dab_input_empty_status(dab_input_t *mi, tvh_input_stream_t *st );

/* TODO: exposing these class methods here is a bit of a hack */
const void *dab_input_class_network_get  ( void *o );
int         dab_input_class_network_set  ( void *o, const void *p );
htsmsg_t   *dab_input_class_network_enum ( void *o, const char *lang );
char       *dab_input_class_network_rend ( void *o, const char *lang );
const void *dab_input_class_active_get   ( void *o );

dab_service_t *dab_service_create0
  ( dab_service_t *ms, const idclass_t *class, const char *uuid,
    dab_ensemble_t *mm, uint16_t sid, htsmsg_t *conf );

#define dab_service_create(t, u, m, s, c)\
  (struct t*)dab_service_create0(calloc(1, sizeof(struct t)),\
                                    &t##_class, u, m, s, c)

#define dab_service_create1(u, m, s, c)\
  dab_service_create0(calloc(1, sizeof(dab_service_t)),\
                         &dab_service_class, u, m, s, c)

dab_service_t *dab_service_create_raw(dab_ensemble_t *mm);

void dab_service_set_subchannel(dab_service_t *s, int16_t SubChId);

void dab_service_unref ( service_t *s );

void dab_service_delete ( service_t *s, int delconf );

dab_service_t *dab_service_find(dab_ensemble_t *mm, uint16_t sid, int create, int *save );

void dab_network_init ( void );
void dab_network_done ( void );

htsmsg_t * dab_network_wizard_get ( dab_input_t *mi, const idclass_t *idc, dab_network_t *mn, const char *lang );
void dab_network_wizard_create ( const char *clazz, htsmsg_t **nlist, const char *lang );

void dab_network_register_builder
  ( const idclass_t *idc,
    dab_network_t *(*build)(const idclass_t *idc, htsmsg_t *conf) );

void dab_network_unregister_builder
  ( const idclass_t *idc );

dab_network_builder_t *dab_network_builder_find  ( const char *clazz );

dab_network_t *dab_network_create0
  ( dab_network_t *mn, const idclass_t *idc, const char *uuid,
    const char *name, htsmsg_t *conf );

#define dab_network_create(t, u, n, c)\
  (struct t*)dab_network_create0(calloc(1, sizeof(struct t)), &t##_class, u, n, c)

extern const idclass_t dab_network_class;

static inline dab_network_t *dab_network_find(const char *uuid)
  { return idnode_find(uuid, &dab_network_class, NULL); }

dab_ensemble_t *dab_network_find_ensemble
  (dab_network_t *mn, uint32_t onid, uint32_t tsid, int check);

dab_service_t *dab_network_find_active_service
  (dab_network_t *mn, uint16_t sid, dab_ensemble_t **rmm);

void dab_network_class_delete ( const idclass_t *idc, int delconf );

void dab_network_delete ( dab_network_t *mn, int delconf );

int dab_network_set_network_name ( dab_network_t *mn, const char *name );
void dab_network_scan ( dab_network_t *mn );
void dab_network_get_type_str( dab_network_t *mn, char *buf, size_t buflen );

dab_network_builder_t *dab_network_builder_find ( const char *clazz );

dab_network_t *dab_network_build
  ( const char *clazz, htsmsg_t *conf );
                 
dab_ensemble_t *dab_ensemble_create0
  ( dab_ensemble_t *mm, const idclass_t *class, const char *uuid,
    dab_network_t *mn, uint32_t onid, htsmsg_t *conf );

#define dab_ensemble_create(type, uuid, mn, onid, conf)\
  (struct type*)dab_ensemble_create0(calloc(1, sizeof(struct type)),\
                                   &type##_class, uuid,\
                                   mn, onid, conf)
#define dab_ensemble_create1(uuid, mn, onid, conf)\
  dab_ensemble_create0(calloc(1, sizeof(dab_ensemble_t)), &dab_ensemble_class, uuid,\
                     mn, onid, conf)

dab_ensemble_t *dab_ensemble_post_create(dab_ensemble_t *mm);

void dab_ensemble_delete ( dab_ensemble_t *mm, int delconf );

void dab_ensemble_free ( dab_ensemble_t *mm );

int dab_ensemble_compare ( dab_ensemble_t *a, dab_ensemble_t *b );

void dab_ensemble_nice_name( dab_ensemble_t *mm, char *buf, size_t len );

void dab_ensemble_update_nice_name( dab_ensemble_t *mm );

int dab_ensemble_set_network_name ( dab_ensemble_t *mm, const char *name );

void dab_ensemble_scan_done (dab_ensemble_t *mm, const char *buf, int res );

int dab_ensemble_class_scan_state_set ( void *, const void * );

static inline int dab_ensemble_scan_state_set ( dab_ensemble_t *m, int state )
  { return dab_ensemble_class_scan_state_set ( m, &state ); }

int dab_ensemble_subscribe(dab_ensemble_t *mm, dab_input_t *mi,
                          const char *name, int weight, int flags);

void dab_ensemble_unsubscribe_by_name(dab_ensemble_t *mm, const char *name);

void dab_ensemble_unsubscribe_linked(dab_input_t *mi, service_t *t);

static inline int dab_ensemble_release ( dab_ensemble_t *mm )
{
  int v = atomic_dec(&mm->mm_refcount, 1);
  assert(v > 0);
  if (v == 1) {
    mm->mm_free(mm);
    return 1;
  }
  return 0;
}

int dab_ensemble_set_onid ( dab_ensemble_t *mm, uint32_t onid );

void dab_ensemble_save ( dab_ensemble_t *mm, htsmsg_t *c, int refs );

void dab_ensemble_tuning_error( const char *ensemble_uuid, dab_ensemble_instance_t *mmi_match );

dab_ensemble_instance_t *dab_ensemble_instance_create0
  ( dab_ensemble_instance_t *mmi, const idclass_t *class, const char *uuid,
    dab_input_t *mi, dab_ensemble_t *mm );

dab_service_t *dab_ensemble_find_service(dab_ensemble_t *ms, uint16_t sid);

#define dab_ensemble_instance_create(type, uuid, mi, mm)\
  (struct type*)dab_ensemble_instance_create0(calloc(1, sizeof(struct type)),\
                                            &type##_class, uuid,\
                                            mi, mm);

void dab_ensemble_instance_delete ( tvh_input_instance_t *tii );

int dab_ensemble_instance_start
  ( dab_ensemble_instance_t **mmiptr, service_t *t, int weight );

/*
 * DAB event handler
 */

typedef struct dab_listener
{
  LIST_ENTRY(dab_listener) ml_link;
  void *ml_opaque;
  void (*ml_ensemble_start)  (dab_ensemble_t *mm, void *p);
  void (*ml_ensemble_stop)   (dab_ensemble_t *mm, void *p, int reason);
  void (*ml_ensemble_create) (dab_ensemble_t *mm, void *p);
  void (*ml_ensemble_delete) (dab_ensemble_t *mm, void *p);
} dab_listener_t;

LIST_HEAD(,dab_listener) dab_listeners;

#define dab_add_listener(ml)\
  LIST_INSERT_HEAD(&dab_listeners, ml, ml_link)

#define dab_rem_listener(ml)\
  LIST_REMOVE(ml, ml_link)

#define dab_fire_event(t, op)\
{\
  dab_listener_t *ml;\
  LIST_FOREACH(ml, &dab_listeners, ml_link)\
    if (ml->op) ml->op(t, ml->ml_opaque);\
} (void)0

#define dab_fire_event1(t, op, arg1)\
{\
  dab_listener_t *ml;\
  LIST_FOREACH(ml, &dab_listeners, ml_link)\
    if (ml->op) ml->op(t, ml->ml_opaque, arg1);\
} (void)0

#endif /* __TVH_DAB_H__ */
