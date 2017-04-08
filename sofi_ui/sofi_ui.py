#!/usr/bin/python3

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

from matplotlib.figure import Figure
from matplotlib.backends.backend_gtk3cairo import FigureCanvasGTK3Cairo as FigureCanvas

import numpy as np

import threading

import libsofi

class ScopeWindow(object):
    def __init__(self, glade_src):
        # Read the window layout
        self.builder= Gtk.Builder()
        self.builder.add_objects_from_file(glade_src, ('window1', '') )

        # connect callbacks to methods in this class
        self.builder.connect_signals(self)

        # Prepare a linear x-axis and an empty
        # y-axis for plotting.
        # Write one to one of the y-values so matplotlib
        # handles the autoscaling correctly
        x_values= np.linspace(0, 1, 1024)
        y_values= np.zeros(1024)
        y_values[0]= 4
        y_values[1]= -4

        # Do whatever matplotlib needs to set up a
        # figure for plotting
        fig = Figure(figsize=(5,5), dpi=100)
        ax = fig.add_subplot(111)
        self.plot= ax.plot(
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
        )[0]
        self.canvas= FigureCanvas(fig)

        # Hook the figure into our window
        sw = self.builder.get_object('plot_window')
        sw.add_with_viewport(self.canvas)

        # Display the window
        scope_window= self.builder.get_object('window1')
        scope_window.show_all()

        self.run= True

        self.bt= threading.Thread(target= self.backend_thread)
        self.bt.start()

    def backend_thread(self):
        self.backend= libsofi.Sofi()

        for mag, phases in self.backend:
            if not self.run:
                return

            GLib.idle_add(self.on_mag_ph_data, mag, phases)

    def on_mag_ph_data(self, mag, phases):
        self.plot.set_ydata(*phases)
        self.canvas.draw()

        return(False)

    def on_window1_destroy(self, widget):
        self.run= False
        Gtk.main_quit()

win= ScopeWindow('sofi_ui.glade')
Gtk.main()
