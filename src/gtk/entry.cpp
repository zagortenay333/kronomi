#include <gtk/gtk.h>
#include "base/core.h"

#define fn(L) G_CALLBACK(+[]()L)

#define APP_ID "org.gtk.Kronomi"

static void activate (GtkApplication *app, Void *data) {
    Auto window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Kronomi");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    Auto box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);

    gtk_window_set_child (GTK_WINDOW(window), box);

    Auto button = gtk_button_new_with_label("Hello World");
    g_signal_connect(button, "clicked", fn({ printf("hhhhhhhhhhhhh\n"); }), NULL);
    gtk_box_append(GTK_BOX(box), button);

    gtk_window_present(GTK_WINDOW(window));
}

Int gtk_run (Int argc, Char **argv) {
    Auto app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    Int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
