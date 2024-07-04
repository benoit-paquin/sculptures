import time
import random
import tkinter as tk
import math


def init_all_x(n, x):
    return [x] * n


def choose(choice1, choice2):
    # choices is ((segment_no,direction),(segment_no,direction))
    if random.randint(0, 1) == 0:
        return choice1
    else:
        return choice2


def get_new_segment(segment_number, direction):
    match direction:
        case 0:
            match segment_number:
                case 0:
                    # 2 backward or 3 backward
                    res = choose((2, 0), (3, 0))
                case 1:
                    # 0 backward or 4 backward
                    res = choose((0, 0), (4, 0))
                case 2:
                    # 1 backward of 5 backward
                    res = choose((1, 0), (5, 0))
                case 3:  # at the center, coming down from 12
                    # 4 forward or 5 forward
                    res = choose((4, 1), (5, 1))
                case 4:
                    # 3 forward or 5 forward
                    res = choose((3, 1), (5, 1))
                case 5:
                    # 3 forward or 4 forward
                    res = choose((3, 1), (4, 1))
        case 1:
            match segment_number:
                case 0:
                    # 1 forward or 4 backward
                    res = choose((1, 1), (4, 0))
                case 1:
                    # 2 forward or 5 backward
                    res = choose((2, 1), (5, 0))
                case 2:
                    # 0 forward of 3 backward
                    res = choose((0, 1), (3, 0))
                case 3:  # vertical, coming to 12
                    # 1 forward or 2 backward
                    res = choose((1, 1), (2, 0))
                case 4:
                    # 0 backward or 1 forward
                    res = choose((0, 0), (1, 1))
                case 5:
                    # 2 forward or 1 backward
                    res = choose((2, 1), (1, 0))
    return res


def advance_snake(snake):
    head = snake[0]
    tail = snake[1]
    # determine direction of the head
    if head[2] == 1:
        if head[1] < 19:
            head[1] = head[1] + 1
        else:
            # find a new segment
            res = get_new_segment(head[0], head[2])
            head[0] = res[0]
            head[2] = res[1]
            if head[2] == 0:
                head[1] = 19
            else:
                head[1] = 0
    else:
        if head[1] > 0:
            head[1] = head[1] - 1
        else:
            res = get_new_segment(head[0],head[2])
            head[0] = res[0]
            head[2] = res[1]
            if head[2] == 0:
                head[1] = 19
            else:
                head[1] = 0
            
    if tail[2] == 1:
        if tail[1] < 19:
            tail[1] = tail[1] + 1
        else:
            tail[0] = head[0]
            tail[2] = head[2]
            if (tail[2] == 0):
                tail[1] = 19
            else:
                tail[1] = 0
    else:
        if tail[1] > 0:
            tail[1] = tail[1] -1
        else:
            tail[0] = head[0]
            tail[2] = head[2]
            if (tail[2] == 0):
                tail[1] = 19
            else:
                tail[1] = 0
    print(head, tail)


def main():
    # clocks
    # 6 segments
    #S0 = init_all_x(20, 0)  # element 0 is minute 0
    #S1 = init_all_x(20, 0)  # element 0 is minute 20
    #S2 = init_all_x(20, 0)  # element 0 is minute 40
    #S4 = init_all_x(20, 0)  # element 0 if in the center, element 19 at 12 o'clock
    #S5 = init_all_x(20, 0)  # element 19 is at 4 o'clock
    #S6 = init_all_x(20, 0)  # element 19 is at 8 o'clock
    # 2 vectors composed of a triplet of segment information
    # head of snake written as (segment_no, pixel_no, direction)
    # end of snakr (segment_id, pixel_no, direction)
    # when the head of the snake comes at the end of a segment, a randon decision moves the head into a different segment.
    # every half a second, the snake goes forward.
    snake0 = [[0, 5, 1], [3, 15, 1]]  # snake is 10 long from 55 to 5 minutes
    #snake1 = [[1, 15, 1], [1, 5, 1]]  # snake is 10 long from 25 to 35 minutes
    for x in range(40):
        advance_snake(snake0)
class display_clock_cls(tk.Tk):
    def __init__(self, *args, **kwargs):
        tk.Tk.__init__(self, *args, **kwargs)
        self.win = tk.Tk()
        #self.win.geometry("800x800")
        self.clock = tk.Label(self, text="hello")
        self.clock.pack()
        #self.c = tk.Canvas(self.win, width=800, height=800)
        #self.c.pack()

        # start the clock "ticking"
        #self.update_snake()

    def update_snake(self):
        now = time.strftime("%H:%M:%S" , time.gmtime())
        self.clock.configure(text=now)
        # call this function again in one second
        self.after(1000, self.update_snake)
    
def display_clock():
    window = tk.Tk()
    window.geometry("800x800")
    greeting = tk.Label(text="Hello, Tkinter")
    greeting.pack()
    c= tk.Canvas(window,width=800, height=800)
    c.pack()
    r=360 #rayon
    for deg in range(0,60):
        y=round(400-r*math.sin(math.radians(deg*6)))
        x=round(400+r*math.cos(math.radians(deg*6)))
        c.create_oval(x-5,y-5,x+5,y+5,fill="#f50")
    for step in range(0,10):
        x=400
        y=400-step*(360/10)
        #c.create_oval(x-5,y-5,x+5,y+5,fill="#f50")
    for step in range(0,10):
        for deg in [90, 90+120, 90+2*120]:
            y=round(400-(step*36)*math.sin(math.radians(deg)))
            x=round(400+(step*36)*math.cos(math.radians(deg)))
            c.create_oval(x-5,y-5,x+5,y+5,fill="#f50")
    for step in range(0,10):
        for deg in [90, 90+120, 90+2*120]:
            y=round(400-(step*36)*math.sin(math.radians(deg)))
            x=round(400+(step*36)*math.cos(math.radians(deg)))
            c.create_oval(x-5,y-5,x+5,y+5,fill="#5f0")
    window.mainloop()
    
#display_clock()
app = display_clock_cls()
app.mainloop()
#main()