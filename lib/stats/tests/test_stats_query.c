/*
 * Copyright (c) 2017 Balabit
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */


#include "apphook.h"
#include "logmsg/logmsg.h"
#include "stats/stats-cluster.h"
#include "stats/stats-counter.h"
#include "stats/stats-query.h"
#include "stats/stats-registry.h"
#include "syslog-ng.h"

#include <criterion/criterion.h>
#include <criterion/parameterized.h>


typedef struct _CounterHashContent
{
  const gint component;
  const gchar *id;
  const gchar *instance;
  const gint type;
} CounterHashContent;

typedef struct _QueryTestCase
{
  const gchar *pattern;
  const gchar *expected;
} QueryTestCase;


static gchar *
_construct_view_name(const gchar *counter_id)
{
  GString *aliased_name = g_string_new("");
  aliased_name = g_string_append(aliased_name, counter_id);
  aliased_name = g_string_append(aliased_name, ".aliased");
  return g_string_free(aliased_name, FALSE);
}

static GList *
_construct_view_query_list(const gchar *counter_instance)
{
  GList *queries = NULL;
  GString *query = g_string_new("*");
  query = g_string_append(query, counter_instance);
  query = g_string_append(query, ".*");
  gchar *q = g_string_free(query, FALSE);
  queries = g_list_append(queries, q);
  return queries;
}

void
_add_two_to_value(GList *counters, StatsCounterItem **result)
{
  StatsCounterItem *c = counters->data;
  stats_counter_set(*result, stats_counter_get(c) + 2);
}

static void
_initialize_counter_hash(void)
{
  size_t i, n;
  const CounterHashContent counters[] =
  {
    {SCS_CENTER, "guba.polo", "frozen", SC_TYPE_SUPPRESSED},
    {SCS_FILE | SCS_SOURCE, "guba", "processed", SC_TYPE_PROCESSED},
    {SCS_GLOBAL, "guba.gumi.diszno", "frozen", SC_TYPE_SUPPRESSED},
    {SCS_PIPE | SCS_SOURCE, "guba.gumi.disz", "frozen", SC_TYPE_SUPPRESSED},
    {SCS_TCP | SCS_DESTINATION, "guba.labda", "received", SC_TYPE_DROPPED},
    {SCS_TCP | SCS_SOURCE, "guba.frizbi", "left", SC_TYPE_STORED},
  };

  app_startup();
  stats_init();
  stats_lock();

  n = sizeof(counters) / sizeof(counters[0]);
  for (i = 0; i < n; i++)
    {
      StatsCounterItem *item = NULL;
      StatsClusterKey sc_key;
      stats_cluster_logpipe_key_set(&sc_key, counters[i].component, counters[i].id, counters[i].instance);
      stats_register_counter(0, &sc_key, counters[i].type, &item);

      gchar *name = _construct_view_name(counters[i].id);
      GList *queries = _construct_view_query_list(counters[i].instance);
      stats_register_view(name, queries, _add_two_to_value);
    }

  stats_unlock();
}

static gboolean
_test_format_log_msg_get(StatsCounterItem *ctr, gpointer user_data)
{
  gchar *name, *value;
  LogMessage *msg = (LogMessage *)user_data;

  name = g_strdup_printf("%s", stats_counter_get_name(ctr));
  value = g_strdup_printf("%"G_GSIZE_FORMAT, stats_counter_get(ctr));

  log_msg_set_value_by_name(msg, name, value, -1);

  g_free(name);
  g_free(value);

  return TRUE;
}

static gboolean
_test_format_str_get(StatsCounterItem *ctr, gpointer user_data)
{
  GString *str = (GString *)user_data;
  g_string_append_printf(str, "%s: %"G_GSIZE_FORMAT"\n", stats_counter_get_name(ctr), stats_counter_get(ctr));

  return TRUE;
}

static gboolean
_test_format_log_msg_get_sum(StatsCounterItem *ctr, gpointer user_data)
{
  gchar *name, *value;
  gpointer *args = (gpointer *) user_data;
  LogMessage *msg = (LogMessage *) args[0];
  gint *sum = (gint *) args[1];

  name = g_strdup_printf("%s", stats_counter_get_name(ctr));
  value = g_strdup_printf("%d", *sum);

  log_msg_set_value_by_name(msg, name, value, -1);

  g_free(name);
  g_free(value);

  return TRUE;
}

static gboolean
_test_format_str_get_sum(StatsCounterItem *ctr, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  GString *result = (GString *) args[0];
  gint *sum = (gint *) args[1];

  g_string_printf(result, "%d", *sum);

  return TRUE;
}

static gboolean
_test_format_list(StatsCounterItem *ctr, gpointer user_data)
{
  GString *str = (GString *)user_data;
  g_string_append_printf(str, "%s\n", stats_counter_get_name(ctr));

  return TRUE;
}


TestSuite(cluster_query_key, .init = app_startup, .fini = app_shutdown);

Test(cluster_query_key, test_global_key)
{
  const gchar *expected_key = "dst.file.d_file.instance";
  StatsClusterKey sc_key;
  stats_cluster_logpipe_key_set(&sc_key, SCS_DESTINATION|SCS_FILE, "d_file", "instance" );
  StatsCluster *sc = stats_cluster_new(&sc_key);
  cr_assert_str_eq(sc->query_key, expected_key,
                   "generated query key(%s) does not match to the expected key(%s)",
                   sc->query_key, expected_key);
}

TestSuite(stats_query, .init = _initialize_counter_hash, .fini = app_shutdown);

ParameterizedTestParameters(stats_query, test_stats_query_get_log_msg_out)
{
  static QueryTestCase test_cases[] =
  {
    {"dst.tcp.guba.labda.received.dropped", "0"},
    {"src.pipe.guba.gumi.disz.frozen.suppressed", "0"},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_log_msg_out)
{
  const gchar *actual;
  LogMessage *msg = log_msg_new_empty();

  stats_query_get(test_cases->pattern, _test_format_log_msg_get, (gpointer)msg);
  actual = log_msg_get_value_by_name(msg, test_cases->pattern, NULL);
  cr_assert_str_eq(actual, test_cases->expected,
                   "Counter: '%s'; expected number: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, actual);

  log_msg_unref(msg);
}

ParameterizedTestParameters(stats_query, test_stats_query_get_str_out)
{
  static QueryTestCase test_cases[] =
  {
    {
      "*.*", "dst.tcp.guba.labda.received.dropped: 0\n"
      "global.guba.gumi.diszno.frozen.suppressed: 0\n"
      "src.tcp.guba.frizbi.left.stored: 0\n"
      "src.file.guba.processed.processed: 0\n"
      "center.guba.polo.frozen.suppressed: 0\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"
      "guba.frizbi.aliased: 2\n"
      "guba.gumi.diszno.aliased: 2\n"
      "guba.polo.aliased: 2\n"
      "guba.aliased: 2\n"
      "guba.gumi.disz.aliased: 2\n"
      "guba.labda.aliased: 2\n"
    },
    {"center.*.*", "center.guba.polo.frozen.suppressed: 0\n"},
    {"cent*", "center.guba.polo.frozen.suppressed: 0\n"},
    {"src.pipe.guba.gumi.disz.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"src.pipe.guba.gumi.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"src.pipe.guba.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"src.pipe.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {
      "*.tcp.guba.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"
      "src.tcp.guba.frizbi.left.stored: 0\n"
    },
    {
      "*.guba.*i.*.*", "global.guba.gumi.diszno.frozen.suppressed: 0\n"
      "src.tcp.guba.frizbi.left.stored: 0\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"
    },
    {
      "*.guba.gum?.*.*", "global.guba.gumi.diszno.frozen.suppressed: 0\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"
    },
    {
      "src.*.*", "src.tcp.guba.frizbi.left.stored: 0\n"
      "src.file.guba.processed.processed: 0\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"
    },
    {"dst.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"},
    {"dst.*.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"},
    {"dst.*.*.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"},
    {"src.java.*.*", ""},
    {"src.ja*.*.*", ""},
    {
      "*.aliased",
      "guba.frizbi.aliased: 2\n"
      "guba.gumi.diszno.aliased: 2\n"
      "guba.polo.aliased: 2\n"
      "guba.aliased: 2\n"
      "guba.gumi.disz.aliased: 2\n"
      "guba.labda.aliased: 2\n"
    },
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_str_out)
{
  GString *result = g_string_new("");

  stats_query_get(test_cases->pattern, _test_format_str_get, (gpointer)result);
  cr_assert_str_eq(result->str, test_cases->expected,
                   "Pattern: '%s'; expected key and value: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, result->str);

  g_string_free(result, TRUE);
}

ParameterizedTestParameters(stats_query, test_stats_query_get_sum_log_msg_out)
{
  static QueryTestCase test_cases[] =
  {
    {"dst.tcp.guba.labda.received.dropped", "0"},
    {"src.pipe.guba.gumi.disz.frozen.suppressed", "0"},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_sum_log_msg_out)
{
  const gchar *actual;
  LogMessage *msg = log_msg_new_empty();

  stats_query_get_sum(test_cases->pattern, _test_format_log_msg_get_sum, (gpointer)msg);
  actual = log_msg_get_value_by_name(msg, test_cases->pattern, NULL);
  cr_assert_str_eq(actual, test_cases->expected,
                   "Pattern: '%s'; expected number: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, actual);

  log_msg_unref(msg);
}

ParameterizedTestParameters(stats_query, test_stats_query_get_sum_str_out)
{
  static QueryTestCase test_cases[] =
  {
    {"*.*", "12"},
    {"center.*.*", "0"},
    {"cent*", "0"},
    {"src.pipe.guba.gumi.disz.*.*", "0"},
    {"src.pipe.guba.gumi.*.*", "0"},
    {"src.pipe.guba.*.*", "0"},
    {"src.pipe.*.*", "0"},
    {"*.tcp.guba.*.*", "0"},
    {"*.guba.*i.*.*", "0"},
    {"*.guba.gum?.*.*", "0"},
    {"src.*.*", "0"},
    {"dst.*.*", "0"},
    {"dst.*.*.*", "0"},
    {"dst.*.*.*.*", "0"},
    {"src.java.*.*", ""},
    {"src.ja*.*.*", ""},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_sum_str_out)
{
  GString *result = g_string_new("");

  stats_query_get_sum(test_cases->pattern, _test_format_str_get_sum, (gpointer)result);
  cr_assert_str_eq(result->str, test_cases->expected,
                   "Pattern: '%s'; expected key and value: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, result->str);

  g_string_free(result, TRUE);
}

ParameterizedTestParameters(stats_query, test_stats_query_list)
{
  static QueryTestCase test_cases[] =
  {
    {
      NULL,
      "dst.tcp.guba.labda.received.dropped\n"
      "global.guba.gumi.diszno.frozen.suppressed\n"
      "src.tcp.guba.frizbi.left.stored\n"
      "src.file.guba.processed.processed\n"
      "center.guba.polo.frozen.suppressed\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed\n"
      "guba.frizbi.aliased\n"
      "guba.gumi.diszno.aliased\n"
      "guba.polo.aliased\n"
      "guba.aliased\n"
      "guba.gumi.disz.aliased\n"
      "guba.labda.aliased\n"
    },
    {
      "*.*",
      "dst.tcp.guba.labda.received.dropped\n"
      "global.guba.gumi.diszno.frozen.suppressed\n"
      "src.tcp.guba.frizbi.left.stored\n"
      "src.file.guba.processed.processed\n"
      "center.guba.polo.frozen.suppressed\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed\n"
      "guba.frizbi.aliased\n"
      "guba.gumi.diszno.aliased\n"
      "guba.polo.aliased\n"
      "guba.aliased\n"
      "guba.gumi.disz.aliased\n"
      "guba.labda.aliased\n"
    },
    {"center.*.*", "center.guba.polo.frozen.suppressed\n"},
    {"cent*", "center.guba.polo.frozen.suppressed\n"},
    {"src.pipe.guba.gumi.disz.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed\n"},
    {"src.pipe.guba.gumi.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed\n"},
    {"src.pipe.guba.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed\n"},
    {"src.pipe.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed\n"},
    {
      "*.tcp.guba.*.*", "dst.tcp.guba.labda.received.dropped\n"
      "src.tcp.guba.frizbi.left.stored\n"
    },
    {
      "*.guba.*i.*.*", "global.guba.gumi.diszno.frozen.suppressed\n"
      "src.tcp.guba.frizbi.left.stored\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed\n"
    },
    {
      "*.guba.gum?.*.*", "global.guba.gumi.diszno.frozen.suppressed\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed\n"
    },
    {
      "src.*.*", "src.tcp.guba.frizbi.left.stored\n"
      "src.file.guba.processed.processed\n"
      "src.pipe.guba.gumi.disz.frozen.suppressed\n"
    },
    {"dst*", "dst.tcp.guba.labda.received.dropped\n"},
    {"dst.*", "dst.tcp.guba.labda.received.dropped\n"},
    {"dst.*.*", "dst.tcp.guba.labda.received.dropped\n"},
    {"dst.*.*.*", "dst.tcp.guba.labda.received.dropped\n"},
    {"dst.*.*.*.*", "dst.tcp.guba.labda.received.dropped\n"},
    {"src.java.*.*", ""},
    {"src.ja*.*.*", ""},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_list)
{
  GString *result = g_string_new("");

  stats_query_list(test_cases->pattern, _test_format_list, (gpointer)result);
  cr_assert_str_eq(result->str, test_cases->expected,
                   "Pattern: '%s'; expected key: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, result->str);

  g_string_free(result, TRUE);
}
