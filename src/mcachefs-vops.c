#include "mcachefs.h"
#include "mcachefs-journal.h"
#include "mcachefs-transfer.h"
#include "mcachefs-vops.h"

static const char *read_state_names[] =
    { "normal", "full", "handsup", "nocache", "quitting", NULL };

static const char *write_state_names[] =
    { "cache", "flush", "force", NULL };

void
mcachefs_vops_cleanup_vops(struct mcachefs_file_t *mvops)
{
    if (mvops->contents)
    {
        Log("VOPS CLEANUP for %s\n", mvops->path);
        free(mvops->contents);
        mvops->contents = NULL;
    }
    mvops->contents_size = 0;
    mvops->contents_alloced = 0;
}

#if 0
const char *mcachefs_vops_list[] =
{   "state", "wrstate", "file_thread_interval", "file_ttl", "timeslices",
    "metadata", "metadata_flush", "metadata_fill", "journal",
    "apply_journal", "drop_journal", "transfer", "transfer_rate",
    "cleanup_backing", "cleanup_backing_list", "cleanup_backing_prefix",
    "cleanup_backing_age", NULL};
#endif

typedef int
(*proc_get_int)();

typedef void
(*proc_set_int)(int);

typedef void
(*proc_extern_build)(struct mcachefs_file_t* mvops);

struct mcachefs_vops_proc
{
    char* name;
    proc_get_int get_int;
    proc_set_int set_int;
    const char** int_string_map;
    proc_extern_build extern_build;
};

struct mcachefs_vops_proc vops_procs[] =
    {
        { "read_state", &mcachefs_config_get_read_state,
                &mcachefs_config_set_read_state, read_state_names, NULL },
        { "write_state", &mcachefs_config_get_write_state,
                &mcachefs_config_set_write_state, write_state_names, NULL },
        { "file_thread_interval", &mcachefs_config_get_file_thread_interval,
                &mcachefs_config_set_file_thread_interval, NULL, NULL },
        { "file_ttl", &mcachefs_config_get_file_ttl,
                &mcachefs_config_set_file_ttl, NULL, NULL },
        { "transfer_max_rate", &mcachefs_config_get_transfer_max_rate,
                &mcachefs_config_set_transfer_max_rate, NULL, NULL },
        { "transfer", NULL, NULL, NULL, &mcachefs_transfer_dump },
        { "journal", NULL, NULL, NULL, &mcachefs_journal_dump },
        { "metadata", NULL, NULL, NULL, &mcachefs_metadata_dump },
        { NULL, NULL, NULL, NULL, NULL } };

const char** names = NULL;

const char **
mcachefs_vops_get_vops_list()
{
    if (!names)
    {
        int number_of_entries = sizeof(vops_procs)
                / sizeof(struct mcachefs_vops_proc);
        Log("Defining %d entries\n", number_of_entries);
        names = (const char**) malloc(sizeof(char*) * number_of_entries);

        int i;
        for (i = 0; vops_procs[i].name != NULL ; i++)
        {
            Log("Defined vops : %s\n", vops_procs[i].name);
            names[i] = vops_procs[i].name;
        }
    }
    return names;
}

struct mcachefs_vops_proc*
mcachefs_vops_proc_find(struct mcachefs_file_t *mvops)
{
    const char *file = &(mvops->path[11]);

    int i;
    for (i = 0; vops_procs[i].name != NULL ; i++)
    {
        if (strcmp(vops_procs[i].name, file) == 0)
        {
            return &(vops_procs[i]);
        }
    }
    Err("Invalid vops proc file '%s'\n", file);
    return NULL ;
}

void
vops_build_int_map(struct mcachefs_file_t* mvops,
        struct mcachefs_vops_proc* proc)
{
    int value = proc->get_int();

    int valueindex = 0;
    for (; proc->int_string_map[valueindex] != NULL ; valueindex++)
    {
        if (valueindex)
        {
            __VOPS_WRITE(mvops, " ");
        }
        if (value == valueindex)
        {
            __VOPS_WRITE(mvops, "[");
        }
        __VOPS_WRITE(mvops, "%s", proc->int_string_map[valueindex]);
        if (value == valueindex)
        {
            __VOPS_WRITE(mvops, "]");
        }
    }
    __VOPS_WRITE(mvops, "\n");
}

void
vops_build_int(struct mcachefs_file_t* mvops, struct mcachefs_vops_proc* proc)
{
    int value = proc->get_int();
    Log("Set value %d for vops=%s\n", value, mvops->path);
    __VOPS_WRITE(mvops, "%d\n", value);
}

void
mcachefs_vops_build(struct mcachefs_file_t *mvops)
{
    if (mvops->contents)
    {
        Log("VOPS file '%s' already has data, skipping.\n", mvops->path);
        return;
    }

    struct mcachefs_vops_proc* proc = mcachefs_vops_proc_find(mvops);
    if (!proc)
    {
        return;
    }

    if (proc->get_int != NULL )
    {
        if (proc->int_string_map != NULL )
        {
            vops_build_int_map(mvops, proc);
        }
        else
        {
            vops_build_int(mvops, proc);
        }
        return;
    }
    if (proc->extern_build != NULL )
    {
        proc->extern_build(mvops);
        return;
    }
}

int
vops_parse_int_raw_map(struct mcachefs_file_t *mvops, const char *values[])
{
    int iter;
    off_t t;

    for (t = 0; t < mvops->contents_alloced; t++)
    {
        if (mvops->contents[t] == '\n')
        {
            mvops->contents[t] = '\0';
            break;
        }
    }

    for (iter = 0; values[iter]; iter++)
    {
        if (strcmp(mvops->contents, values[iter]) == 0)
            return iter;

    }
    return -1;
}

void
vops_parse_int_map(struct mcachefs_file_t* mvops,
        struct mcachefs_vops_proc* proc)
{
    int value = vops_parse_int_raw_map(mvops, proc->int_string_map);

    if (value != -1)
    {
        Log("Setting value %d for file=%s\n", value, mvops->path);
        proc->set_int(value);
    }
    else
    {
        Err("Invalid value %s for file %s\n", mvops->contents, mvops->path);
    }
}

void
vops_parse_int(struct mcachefs_file_t* mvops, struct mcachefs_vops_proc* proc)
{
    off_t t;

    for (t = 0; t < mvops->contents_alloced; t++)
    {
        if (mvops->contents[t] == '\n')
        {
            mvops->contents[t] = '\0';
            break;
        }
    }

    int result = atoi(mvops->contents);
    proc->set_int(result);
}

void
mcachefs_vops_parse(struct mcachefs_file_t *mvops)
{
    struct mcachefs_vops_proc* proc = mcachefs_vops_proc_find(mvops);
    if (!proc)
    {
        return;
    }

    if (proc->set_int != NULL )
    {
        if (proc->int_string_map != NULL )
        {
            vops_parse_int_map(mvops, proc);
        }
        else
        {
            vops_parse_int(mvops, proc);
        }
        return;
    }
}

#if 0

int mcachefs_state = MCACHEFS_STATE_NORMAL;
int mcachefs_wrstate = MCACHEFS_WRSTATE_CACHE;
int mcachefs_file_thread_interval = 1;

int mcachefs_file_ttl = 300;
int mcachefs_metadata_ttl = 120;
off_t mcachefs_transfer_max_rate = 100000;

int mcachefs_cleanup_backing_age = 30 * 24 * 3600;
char *mcachefs_cleanup_backing_prefix = NULL;

void
mcachefs_config_set_read_state (int state)
{
    mcachefs_state = state;
}

int
mcachefs_config_get_read_state ()
{
    return mcachefs_state;
}

void
mcachefs_config_set_write_state (int wrstate)
{
    mcachefs_wrstate = wrstate;
}

int
mcachefs_config_get_write_state ()
{
    return mcachefs_wrstate;
}

int
mcachefs_config_get_file_thread_interval ()
{
    return mcachefs_file_thread_interval;
}

int
mcachefs_config_get_file_ttl ()
{
    return mcachefs_file_ttl;
}

int
mcachefs_get_metadata_ttl ()
{
    return mcachefs_metadata_ttl;
}

off_t
mcachefs_config_get_transfer_max_rate ()
{
    return mcachefs_transfer_max_rate;
}

int
mcachefs_config_get_cleanup_cache_age ()
{
    return mcachefs_cleanup_backing_age;
}

const char *
mcachefs_config_get_cleanup_cache_prefix ()
{
    if (mcachefs_cleanup_backing_prefix)
    return mcachefs_cleanup_backing_prefix;
    return "/";
}

#endif

#if 0

void
mcachefs_vops_build(struct mcachefs_file_t *mvops)
{
    if (mvops->contents)
    {
        Log("VOPS file '%s' already has data, skipping.\n", mvops->path);
        return;
    }

    const char *file = &(mvops->path[11]);

    Log("VOPS BUILD : file : '%s', path='%s'\n", file, mvops->path);

    if (strcmp(file, state_file) == 0)
    {
        __VOPS_WRITE(mvops,
                "%s", read_state_names[mcachefs_config_get_read_state ()]);
    }
    else if (strcmp(file, wrstate_file) == 0)
    {
        __VOPS_WRITE(mvops,
                "%s", write_state_names[mcachefs_config_get_write_state ()]);
    }
    else if (strcmp(file, "file_thread_interval") == 0)
    {
        __VOPS_WRITE(mvops, "%d", mcachefs_config_get_file_thread_interval ());
    }
    else if (strcmp(file, "file_ttl") == 0)
    {
        __VOPS_WRITE(mvops, "%d", mcachefs_config_get_file_ttl ());
    }
    else if (strcmp(file, "transfer_rate") == 0)
    {
        __VOPS_WRITE(mvops,
                "%lu", (unsigned long) mcachefs_config_get_transfer_max_rate());
    }
    else if (strcmp(file, "metadata_flush") == 0)
    {
        __VOPS_WRITE(mvops, "Write '1' to flush.\n");
    }
    else if (strcmp(file, "timeslices") == 0)
    {
        mcachefs_file_timeslices_dump(mvops);
    }
    else if (strcmp(file, "metadata") == 0)
    {
        mcachefs_metadata_dump(mvops);
    }
    else if (strcmp(file, "journal") == 0)
    {
        mcachefs_journal_dump(mvops);
    }
    else if (strcmp(file, "transfer") == 0)
    {
        mcachefs_transfer_dump(mvops);
    }
    else if (strcmp(file, "cleanup_backing_list") == 0)
    {
        mcachefs_cleanup_backing(mvops, 1);
    }
    else if (strcmp(file, "cleanup_backing_prefix") == 0)
    {
        __VOPS_WRITE(mvops, "%s", mcachefs_config_get_cleanup_cache_prefix ());
    }
    else if (strcmp(file, "cleanup_backing_age") == 0)
    {
        __VOPS_WRITE(mvops,
                "%lu", (unsigned long) mcachefs_config_get_cleanup_cache_age ());
    }
    else
    {
        Err("Unknown VOPS file : '%s'\n", file);
    }
}

int
mcachefs_vops_parse_int(struct mcachefs_file_t *mvops, const char *values[])
{
    int iter;
    off_t t;

    Log(
            "Parsing : raw='%s', size=%lu, alloced=%lu\n", mvops->contents, (unsigned long) mvops->contents_size, (unsigned long) mvops->contents_alloced);

    for (t = 0; t < mvops->contents_alloced; t++)
    {
        if (mvops->contents[t] == '\n')
        {
            mvops->contents[t] = '\0';
            break;
        }
    }

    for (iter = 0; values[iter]; iter++)
    {
        if (strcmp(mvops->contents, values[iter]) == 0)
        return iter;

    }
    return -1;
}

void
mcachefs_vops_parse(struct mcachefs_file_t *mvops)
{
    const char *file = &(mvops->path[11]);
//    char *c;
    int result;
    off_t result_off_t;

    Log("VOPS PARSE : file : '%s', path='%s'\n", file, mvops->path);

    if (strcmp(file, state_file) == 0)
    {
        if ((result = mcachefs_vops_parse_int(mvops, read_state_names)) == -1)
        {
            Err("Invalid state value : '%s'\n", mvops->contents);
        }
        else
        {
            Info("Setting state '%d' : '%s'\n", result, read_state_names[result]);
            mcachefs_config_set_read_state(result);
        }
    }
#if 0
    else if (strcmp (file, wrstate_file) == 0)
    {
        __VOPS_WRITE (mvops, "%s", write_state_names[mcachefs_config_get_write_state ()]);
    }
#endif
    else if (strcmp(file, "file_thread_interval") == 0)
    {
        result = atoi(mvops->contents);
        if (result > 0 && result < 100)
        {
            mcachefs_config_set_file_thread_interval(result);
        }
        else
        {
            Err("Invalid result for file_thread_interval\n");
        }
    }
    else if (strcmp(file, "file_ttl") == 0)
    {
        result = atoi(mvops->contents);
        if (result > 0 && result < mcachefs_file_timeslice_nb)
        {
            mcachefs_config_set_file_ttl(result);
        }
        else
        {
            Err("Invalid result for file_ttl\n");
        }
    }
    else if (strcmp(file, "transfer_rate") == 0)
    {
        result_off_t = (off_t) atol(mvops->contents);
        if (0 <= result_off_t)
        {
            mcachefs_config_set_transfer_max_rate(result_off_t);
            Log(
                    "Set transfer_rate to %lu\n", (unsigned long) mcachefs_transfer_max_rate);
        }
        else
        {
            Err(
                    "Invalid result for transfer_rate : '%s' => %lu\n", mvops->contents, (unsigned long) result_off_t);
        }
    }
    else if (strcmp(file, "metadata_flush") == 0)
    {
        result = atoi(mvops->contents);
        if (result == 1)
        {
            mcachefs_metadata_flush();
        }
        else
        {
            mcachefs_metadata_flush_entry(mvops->contents);
        }
    }
    else if (strcmp(file, "metadata_fill") == 0)
    {
        mcachefs_metadata_fill(mvops->contents);
    }
    else if (strcmp(file, "apply_journal") == 0)
    {
        if (strncmp(mvops->contents, "apply\n", 6) == 0)
        mcachefs_journal_apply();
        else
        Err("Invalid value for %s : '%s'\n", file, mvops->contents);
    }
    else if (strcmp(file, "drop_journal") == 0)
    {
        if (strncmp(mvops->contents, "drop\n", 5) == 0)
        mcachefs_journal_drop();
        else
        Err("Invalid value for %s : '%s'\n", file, mvops->contents);
    }
#if 0
    else if (strcmp(file, "cleanup_backing_prefix") == 0)
    {
        if (mcachefs_cleanup_backing_prefix)
        {
            free(mcachefs_cleanup_backing_prefix);
            mcachefs_cleanup_backing_prefix = NULL;
        }

        Info("contents : '%s'\n", mvops->contents);
        Info("size=%lu\n", (unsigned long) mvops->contents_size);
        mcachefs_cleanup_backing_prefix = (char *) malloc(
                mvops->contents_size + 2);
        memcpy(mcachefs_cleanup_backing_prefix, mvops->contents,
                mvops->contents_size);
        mcachefs_cleanup_backing_prefix[mvops->contents_size] = '\0';

        Info(
                "Set cleanup backing prefix to '%s'\n", mcachefs_cleanup_backing_prefix);

        for (c = mcachefs_cleanup_backing_prefix; *c; c++)
        {
            if (*c == '\n' || *c == '\r')
            {
                *c = '\0';
                break;
            }
        }
        for (c = mcachefs_cleanup_backing_prefix; *c; c++)
        {
            if (c[1] == '\0' && *c != '/')
            {
                c[1] = '/';
                c[2] = '\0';
                break;
            }
        }
        Info(
                "Set cleanup backing prefix to '%s'\n", mcachefs_cleanup_backing_prefix);
    }
    else if (strcmp(file, "cleanup_backing_age") == 0)
    {
        result = atoi(mvops->contents);
        if (result > 0)
        mcachefs_cleanup_backing_age = result;
    }
#endif
    else if (strcmp(file, "cleanup_backing") == 0)
    {
        if (strncmp(mvops->contents, "cleanup\n", 8) == 0)
        mcachefs_cleanup_backing(mvops, 0);
        else
        Err("Invalid value for %s : '%s'\n", file, mvops->contents);
    }
    else
    {
        Err("Unknown VOPS file for writing : '%s'\n", file);
    }

}

#endif
