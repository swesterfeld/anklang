// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "gtk2wrap.hh"
#include <gtk/gtk.h>
#include <semaphore.h>
#include <thread>

namespace { // Anon
using namespace Ase;

static std::thread *gtkthred = nullptr;

// == Semaphore ==
class Semaphore {
  /*copy*/         Semaphore (const Semaphore&) = delete;
  Semaphore& operator=       (const Semaphore&) = delete;
  sem_t sem = {};
public:
  explicit  Semaphore () noexcept { const int err = sem_init (&sem, 0, 0); g_return_if_fail (!err); }
  void      post      () noexcept { sem_post (&sem); }
  void      wait      () noexcept { sem_wait (&sem); }
  /*dtor*/ ~Semaphore () noexcept { sem_destroy (&sem); }
};

// == GBoolean lambda callback ==
using BWrapFunc = std::function<bool()>;
struct BWrap {
  void *data = this;
  void (*deleter) (void*) = [] (void *data) {
    BWrap *self = (BWrap*) data;
    self->deleter = nullptr;
    delete self;
  };
  gboolean (*boolfunc) (void*) = [] (void *data) -> int {
    BWrap *self = (BWrap*) data;
    return self->func();
  };
  gboolean (*truefunc) (void*) = [] (void *data) -> int {
    BWrap *self = (BWrap*) data;
    self->func();
    return true;
  };
  gboolean (*falsefunc) (void*) = [] (void *data) -> int {
    BWrap *self = (BWrap*) data;
    self->func();
    return true;
  };
  const BWrapFunc func;
  BWrap (const BWrapFunc &fun) : func (fun) {}
};
static BWrap* bwrap (const BWrapFunc &fun) {
  return new BWrap (fun);
}
static BWrap* vwrap (const std::function<void()> &fun) {
  return new BWrap ([fun]() { fun(); return false; });
}

// == functions ==
static void
gtkmain()
{
  pthread_setname_np (pthread_self(), "gtk2wrap:thread");
  gdk_threads_init();
  gdk_threads_enter();

  if (1) {
    int argc = 0;
    char **argv = nullptr;
    gtk_init (&argc, &argv);
  }

  gtk_main();
  gdk_threads_leave();
}

static std::unordered_map<ulong,GtkWidget*> windows;

static ulong
create_window (const Gtk2WindowSetup &wsetup)
{
  GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  // g_signal_connect (window, "destroy", gtk_main_quit, nullptr);
  if (wsetup.width > 0 && wsetup.height > 0)
    gtk_window_set_resizable (GTK_WINDOW (window), false);
  if (wsetup.deleterequest_mt) {
    BWrap *bw = vwrap (wsetup.deleterequest_mt);
    g_signal_connect_data (window, "delete-event", (GCallback) bw->truefunc, bw, (GClosureNotify) bw->deleter, G_CONNECT_SWAPPED);
  }
  GtkWidget *socket = gtk_socket_new();
  gtk_container_add (GTK_CONTAINER (window), socket);
  gtk_widget_set_size_request (socket, wsetup.width, wsetup.height);
  gtk_widget_realize (socket);
  ulong windowid = gtk_socket_get_id (GTK_SOCKET (socket));
  windows[windowid] = window;
  gtk_widget_show_all (gtk_bin_get_child (GTK_BIN (window)));
  gtk_window_set_title (GTK_WINDOW (window), wsetup.title.c_str());
  return windowid;
}

static bool
destroy_window (ulong windowid)
{
  auto it = windows.find (windowid);
  if (it == windows.end()) return false;
  GtkWidget *window = it->second;
  windows.erase (it);
  gtk_widget_destroy (window);
  return true;
}

static bool
resize_window (ulong windowid, int width, int height)
{
  auto it = windows.find (windowid);
  if (it == windows.end()) return false;
  GtkWidget *window = it->second;
  gtk_widget_set_size_request (gtk_bin_get_child (GTK_BIN (window)), width, height);
  return true;
}

static bool
show_window (ulong windowid)
{
  auto it = windows.find (windowid);
  if (it == windows.end()) return false;
  GtkWidget *window = it->second;
  gtk_widget_show (window);
  return true;
}

static bool
hide_window (ulong windowid)
{
  auto it = windows.find (windowid);
  if (it == windows.end()) return false;
  GtkWidget *window = it->second;
  gtk_widget_hide (window);
  return true;
}

template<typename Ret, typename ...Args, typename ...Params> static Ret
gtkidle_call (Ret (*func) (Params...), Args &&...args)
{
  if (!gtkthred)        // TODO: clean this up on process exit
    gtkthred = new std::thread (gtkmain);
  Semaphore sem;
  Ret ret = {};
  BWrap *bw = bwrap ([&sem, &ret, func, &args...] () -> bool {
    GDK_THREADS_ENTER ();
    ret = func (std::forward<Args> (args)...);
    GDK_THREADS_LEAVE ();
    sem.post();
    return false;
  });
  g_idle_add_full (G_PRIORITY_HIGH, bw->boolfunc, bw, bw->deleter);
  // See gdk_threads_add_idle_full for LEAVE/ENTER reasoning
  sem.wait();
  return ret;
}

} // Anon

extern "C" {
Ase::Gtk2DlWrapEntry Ase__Gtk2__wrapentry {
  .create_window = [] (const Gtk2WindowSetup &windowsetup) -> ulong {
    return gtkidle_call (create_window, windowsetup);
  },
  .resize_window = [] (ulong windowid, int width, int height) {
    return gtkidle_call (resize_window, windowid, width, height);
  },
  .show_window = [] (ulong windowid) {
    gtkidle_call (show_window, windowid);
  },
  .hide_window = [] (ulong windowid) {
    gtkidle_call (hide_window, windowid);
  },
  .destroy_window = [] (ulong windowid) {
    gtkidle_call (destroy_window, windowid);
  },
  .threads_enter = gdk_threads_enter,
  .threads_leave = gdk_threads_leave,
};
} // "C"
