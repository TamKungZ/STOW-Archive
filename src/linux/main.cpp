#include <gtk/gtk.h>

namespace {

void Activate(GtkApplication* application, gpointer) {
    GtkWidget* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "STOW Archive");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 320);

    GtkWidget* label = gtk_label_new("Hello from native Linux UI");
    gtk_window_set_child(GTK_WINDOW(window), label);
    gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int main(int argc, char* argv[]) {
    GtkApplication* application =
        gtk_application_new("com.tamkungz.stowarchive", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(Activate), nullptr);

    const int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
