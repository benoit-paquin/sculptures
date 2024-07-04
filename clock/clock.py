try:
    import Tkinter
except:
    import tkinter as Tkinter
import math
import time
class clock(Tkinter.Tk):
    def __init__(self):
        Tkinter.Tk.__init__(self)
        self.x = 400 # middle of clock
        self.y = 400
        self.r = 360 # rayon of clock
        self.last_hour = -1
        self.last_min = -1
        self.last_sec = -1
        self.segments = []
        #snake has start segment, starting dot, length, previous segment, previous segment]
        #    2 2  0 0    
        #   2   5    0
        #  2    5     0
        #  2    x     0
        #  2   4 3    0
        #   2 4   3  0
        #    1 1 1 0
        # snake is segment1, start dot, end dot, segment2, startdot, enddot, segment 3, startdot, enddot)
        self.create_all_function_trigger()
    
    def create_all_function_trigger(self):
        self.create_canvas_for_shapes()
        self.create_segments()
        #self.create_timedots()
        return

    def create_canvas_for_shapes(self):
        self.canvas = Tkinter.Canvas(self, bg='black', height=800, width=800)
        self.canvas.pack(expand='yes', fill='both')
        return
    
    def create_segments(self):
        self.segments = []
        segment = []
        colour="#333"
        for deg in range(0,20):
            objId = None
            y=round(400+self.r*math.sin(math.radians(deg*6)-math.radians(90)))
            x=round(400+self.r*math.cos(math.radians(deg*6)-math.radians(90)))
            obj_id=self.canvas.create_oval(x-5,y-5,x+5,y+5,fill=colour)
            segment.append((obj_id,x,y,colour))
        self.segments.append(segment)
        segment = []
        for deg in range(20,40):
            y=round(400+self.r*math.sin(math.radians(deg*6)-math.radians(90)))
            x=round(400+self.r*math.cos(math.radians(deg*6)-math.radians(90)))
            obj_id=self.canvas.create_oval(x-5,y-5,x+5,y+5,fill=colour)
            segment.append((obj_id,x,y,colour))
        self.segments.append(segment)
        segment = []
        for deg in range(40,60):
            y=round(400+self.r*math.sin(math.radians(deg*6)-math.radians(90)))
            x=round(400+self.r*math.cos(math.radians(deg*6)-math.radians(90)))
            obj_id=self.canvas.create_oval(x-5,y-5,x+5,y+5,fill=colour)
            segment.append((obj_id,x,y,colour))
        self.segments.append(segment)
        for deg in [90, 90+120, 90+2*120]:
            segment = []
            for step in range(0,10):
                y=round(400-(step*36)*math.sin(math.radians(deg)))
                x=round(400+(step*36)*math.cos(math.radians(deg)))
                obj_id=self.canvas.create_oval(x-5,y-5,x+5,y+5,fill=colour)
                segment.append((obj_id,x,y,colour))
            self.segments.append(segment)
        return
                 
    def update_tick(self,last, current, colour):
        if last != -1:
            segment_no = int(last/20)
            dot_no = last%20
            #print(segment_no,dot_no)
            #print("resetting segment ",segment_no, " dot ",dot_no, "colour ", colour)
            obj_id = self.segments[segment_no][dot_no][0]
            self.canvas.itemconfigure(obj_id,fill="#333")
        print("current: ",current)
        segment_no = int(current/20)
        dot_no = current%20
        #print("setting segment ",segment_no, " dot ",dot_no, "colour ", colour)
        obj_id = self.segments[segment_no][dot_no][0]
        self.canvas.itemconfigure(obj_id,fill=colour)
        return
    
    def update_time(self):
        now = time.localtime()
        t = time.strptime(str(now.tm_hour), "%H")
        hour = int(time.strftime("%I", t))*5
        if self.last_hour != hour:
            self.update_tick(self.last_hour, hour, "red")
            self.last_hour = hour
        if self.last_min != now.tm_min:
            self.update_tick(self.last_min, now.tm_min, "green")
            self.last_min = now.tm_min
        if self.last_sec != now.tm_sec:
            self.update_tick(self.last_sec, now.tm_sec, "blue")
            self.last_sec=now.tm_sec
        return
    def update_snake(self,snake):
        hseg = snake[3]
        dot = snake[0]
        direc = snake[1]
        length = snake[2]
        other= snake[4:]
        # fill in the first segment
        if direc == 1: #snake is avancing
            for i in range(dot,max(0,dot-length)-1,-1):
                #print("segment: ",hseg, " dot: ", i)
                obj_id = self.segments[hseg][i][0]
                self.canvas.itemconfigure(obj_id,fill="orange")
        else:
            seg_len = len(self.segments[hseg])
            for i in range(dot,min(seg_len,dot+length)):
                #print("segment: ",hseg, " dot: ", i)
                obj_id = self.segments[hseg][i][0]
                self.canvas.itemconfigure(obj_id,fill="orange")
        mid_seg = other[:-1]
        for mseg in mid_seg:
            seg_len = len(self.segments[mseg])
            for i in range(0,seg_len):
                obj_id = self.segments[mseg][i][0]
                self.canvas.itemconfigure(obj_id,fill="orange")
        last_seg = other[-1]
        #determine the direction of the last segment
        if last_seg != hseg:
            previous = snake[-2]
            match (previous*10+last_seg):
                case 1: # goes from seg 0 to 1
                    pass
                case (3):
                    direc = 0
                case (2):
                    pass
                case (5):
                    pass
                case (10):
                    pass
                case (13):
                    pass
                case (12,14):
                    print("hi")
                case (14):
                    pass

            

if __name__ == '__main__':
    root = clock()
    #snake is indicated as head dot, direction(1=increasing dot no), length, head segment, additional segments.
    snake = [5, 0, 30, 0, 1,2] 
    while True:
        root.update()
        root.update_idletasks()
        root.update_time()
        root.update_snake(snake)
    
