# Copyright 2015 University of Rochester
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
# http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License. 


library(plyr)
library(ggplot2)
no_zero <- function(x) {
  y <- sprintf('%.1f',x)
  y[x > 0 & x < 1] <- sprintf('.%s',x[x > 0 & x < 1]*10)
  y[x == 0] <- '0'
  y[x > -1 & x < 0] <- sprintf('-.%s',x[x > -1 & x < 0]*-10)
  y
}
scientific_10 <- function(x) {
  parse(text=gsub("1e\\+", "10^", scales::scientific_format()(x)))
}
tests<-c("thread_striped")
for (t in tests){
read.csv(paste("./graph_",t,".csv",sep=""))->montagedata

montagedata$ds<-as.factor(gsub("NoPersistTGraph","DRAM (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("NoPersistNVMGraph","Montage (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("MontageGraph","Montage",montagedata$ds))
d1<-subset(montagedata,ds=="DRAM (T)")
d2<-subset(montagedata,ds=="Montage (T)")
d3<-subset(montagedata,ds=="Montage")
graphdata = rbind(d1,d2,d3)
edge80<-subset(graphdata,test=="GraphTest:80edge20vertex:degree32")
edge998<-subset(graphdata,test=="GraphTest:99.8edge.2vertex:degree32")

ddply(.data=edge80,.(ds,thread),mutate,ops= mean(ops)/1000000)->edge80

ddply(.data=edge998,.(ds,thread),mutate,ops= mean(ops)/1000000)->edge998

edge80data = rbind(edge80[,c("ds","thread","ops")])
edge80data$ds <- factor(edge80data$ds, levels=c("DRAM (T)", "NVM (T)", "Montage (T)", "Montage", "SOFT", "Dalí", "MOD", "Pronto-Full", "Pronto-Sync", "Mnemosyne"))

edge998data = rbind(edge998[,c("ds","thread","ops")])
edge998data$ds <- factor(edge998data$ds, levels=c("DRAM (T)", "NVM (T)", "Montage (T)", "Montage", "SOFT", "Dalí", "MOD", "Pronto-Full", "Pronto-Sync", "Mnemosyne"))

# Set up colors and shapes (invariant for all plots)
color_key = c("#12E1EA","#1245EA",
              "#FF69B4","#C11B14",
              "#660099","#1BC40F","#5947ff",
              "#FF8C00", "#F86945",
              "#191970")
names(color_key) <- levels(edge80data$ds)

shape_key = c(2,1,0,18,20,17,15,16,62,4)
names(shape_key) <- levels(edge80data$ds)

line_key = c(2,2,2,1,1,1,1,1,4,1)
names(line_key) <- levels(edge80data$ds)

# Benchmark-specific plot formatting
legend_pos=c(0.665,0.7)
y_name="Throughput (Mops/s)"
y_range_down = 100000

# Generate the plots
linchart<-ggplot(data=edge80data,
                  aes(x=thread,y=ops,color=ds, shape=ds, linetype=ds))+
  geom_line()+xlab("Threads")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% edge80data$ds])+
  scale_linetype_manual(values=line_key[names(line_key) %in% edge80data$ds])+
  theme_bw()+ guides(shape=guide_legend(title=NULL))+ 
  guides(color=guide_legend(title=NULL))+
  guides(linetype=guide_legend(title=NULL))+
  scale_color_manual(values=color_key[names(color_key) %in% edge80data$ds])+
  scale_x_continuous(breaks=c(0,10,20,30,40,50,60,70,80,90),minor_breaks=c(-10))+
  scale_y_continuous(label=no_zero)+
  coord_cartesian(xlim = c(-3.5, 65), ylim = c(0,0.4))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 18))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.145,vjust = 8.7,margin = margin(t = 15, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./graph_edge80_",t,".pdf",sep=""),linchart,width=5.3, height = 4, units = "in", dpi=300, title = paste("graph_edge80_",t,".pdf",sep=""))

names(color_key) <- levels(edge998data$ds)

shape_key = c(2,1,0,18,20,17,15,16,62,4)
names(shape_key) <- levels(edge998data$ds)

line_key = c(2,2,2,1,1,1,1,1,4,1)
names(line_key) <- levels(edge998data$ds)

# Benchmark-specific plot formatting
legend_pos=c(0.665,0.3)
y_name="Throughput (Mops/s)"
y_range_down = 100000

# Generate the plots
linchart<-ggplot(data=edge998data,
                  aes(x=thread,y=ops,color=ds, shape=ds, linetype=ds))+
  geom_line()+xlab("Threads")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% edge998data$ds])+
  scale_linetype_manual(values=line_key[names(line_key) %in% edge998data$ds])+
  theme_bw()+ guides(shape=guide_legend(title=NULL))+ 
  guides(color=guide_legend(title=NULL))+
  guides(linetype=guide_legend(title=NULL))+
  scale_color_manual(values=color_key[names(color_key) %in% edge998data$ds])+
  scale_x_continuous(breaks=c(0,10,20,30,40,50,60,70,80,90),minor_breaks=c(-10))+
  coord_cartesian(xlim = c(-3, 65), ylim = c(0,30))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 18))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.16,vjust = 8.7,margin = margin(t = 15, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./graph_edge998_",t,".pdf",sep=""),linchart,width=5.3, height = 4, units = "in", dpi=300, title = paste("graph_edge998_",t,".pdf",sep=""))
}
