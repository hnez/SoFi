#!/usr/bin/python3

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

from matplotlib.figure import Figure
from matplotlib.backends.backend_gtk3cairo import FigureCanvasGTK3Cairo as FigureCanvas

import numpy as np

import threading

import libsofi
from direction import AntennaArray

class ScopeWindow(object):
    def __init__(self, glade_src, antenna_array):
        self.antenna_array= antenna_array

        # Read the window layout
        self.builder= Gtk.Builder()
        self.builder.add_objects_from_file(glade_src, ('window1', '') )

        # connect callbacks to methods in this class
        self.builder.connect_signals(self)

        self.setup_spectrum_window()
        self.setup_direction_window()

        # Display the window
        scope_window= self.builder.get_object('window1')
        scope_window.show_all()

        self.running= True
        self.frame= 0

        self.bt= threading.Thread(target= self.backend_thread)
        self.bt.start()

    def run(self):
        Gtk.main()

    def setup_spectrum_window(self):
        # Prepare a linear x-axis and an empty
        # y-axis for plotting.
        # Write one to one of the y-values so matplotlib
        # handles the autoscaling correctly
        x_values= self.antenna_array.frequencies
        y_values= np.zeros(1024)
        y_values[0]= 3
        y_values[1]= -3

        # Do whatever matplotlib needs to set up a
        # figure for plotting
        fig = Figure(figsize=(5,5), dpi=100)
        ax = fig.add_subplot(111)

        ax.grid(True)
        ax.set_title('Phase spectrum')

        self.spectrum_plots= ax.plot(
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
            x_values, y_values,
            x_values, y_values
        )
        self.spectrum_canvas= FigureCanvas(fig)

        # Hook the figure into our window
        sv = self.builder.get_object('spectrum_view')
        sv.add_with_viewport(self.spectrum_canvas)

    def setup_direction_window(self):
        phi_values= np.linspace(0, 2*np.pi, 1024)
        r_values= np.zeros(1024)
        r_values[0]= 0.1

        # Do whatever matplotlib needs to set up a
        # figure for plotting
        fig = Figure(figsize=(5,5), dpi=100)
        ax = fig.add_subplot(111, projection='polar')

        ax.grid(True)
        ax.set_title('Direction pseudo spectrum')

        self.direction_plots= ax.plot(
            phi_values, r_values,
            phi_values, r_values,
            phi_values, r_values,
            phi_values, r_values,
            phi_values, r_values,
            phi_values, r_values,
        )
        self.direction_canvas= FigureCanvas(fig)

        # Hook the figure into our window
        sv = self.builder.get_object('direction_view')
        sv.add_with_viewport(self.direction_canvas)


    def backend_thread(self):
        self.backend= libsofi.Sofi()

        for mag, phases in self.backend:
            if not self.running:
                return

            GLib.idle_add(self.on_mag_ph_data, mag, phases)

    def on_mag_ph_data(self, mag, phases):
        natural_mag= np.concatenate((mag[512:], mag[:512]))

        natural_phases= tuple(
            np.concatenate((ph[512:], ph[:512]))
            for ph in phases
        )

        if (self.frame%32) == 0:
            self.antenna_array.find_noisepoints(natural_mag)
            self.antenna_array.find_signalpoints(natural_mag)

            print('New noise points:', ', '.join(map(str, self.antenna_array.noise_points)))
            print('New signal points:', ', '.join(map(str, self.antenna_array.signal_points)))

        self.frame+= 1

        dir_infos, clean_phases= self.antenna_array.process_edge_frameset(natural_phases, natural_mag)


        for (meh, muh) in zip(self.spectrum_plots, clean_phases):
            meh.set_ydata(muh)

        self.spectrum_canvas.draw()


        for (meh, muh) in zip(self.direction_plots, dir_infos):
            meh.set_ydata(muh)

        self.direction_canvas.draw()

        return(False)

    def on_window1_destroy(self, widget):
        self.running= False
        Gtk.main_quit()


antenna_array= AntennaArray(
    (
        ( 0.0,  0.0),
        ( 0.0,  0.175),
        ( 0.24, 0.175),
        ( 0.24, 0.0)
    ),
    1024, 1024,
    96.5e6, 98.5e6
)

win= ScopeWindow(
    'sofi_ui.glade',
    antenna_array
)

win.run()
