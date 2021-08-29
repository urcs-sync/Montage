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

scientific_10 <- function(x) {
  parse(text=gsub("1e\\+", "10^", scales::scientific_format()(x)))
}
tests<-c("g0i50r50_thread","g50i25r25_thread","g90i5r5_thread")
for (t in tests){
read.csv(paste("./maps_",t,".csv",sep=""))->montagedata

montagedata$ds<-as.factor(gsub("nbMontageNataTree","nbMontage",montagedata$ds))
montagedata$ds<-as.factor(gsub("PNataTree","Izraelevitz",montagedata$ds))
montagedata$ds<-as.factor(gsub("MontageNataTree","Montage",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVTraverseNataTree","NVTraverse",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVMNataTree","NVM (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("NataTree","DRAM (T)",montagedata$ds))
d1<-subset(montagedata,ds=="DRAM (T)")
d2<-subset(montagedata,ds=="NVM (T)")
d3<-subset(montagedata,ds=="nbMontage")
d4<-subset(montagedata,ds=="Montage")
d5<-subset(montagedata,ds=="NVTraverse")
d6<-subset(montagedata,ds=="Izraelevitz")
lkdata = rbind(d1,d2,d3,d4,d5,d6)

ddply(.data=lkdata,.(ds,thread),mutate,mops= mean(ops)/1000000)->lkdata
lindata = rbind(lkdata[,c("ds","thread","mops")])
lindata$ds <- factor(lindata$ds, levels=c("DRAM (T)", "NVM (T)", "nbMontage", "Montage","Izraelevitz", "NVTraverse"))

# Set up colors and shapes (invariant for all plots)
color_key = c("#12E1EA","#1245EA","#C11B14",
              "#FF69B4","#809900","#1BC40F")
names(color_key) <- levels(lindata$ds)

shape_key = c(2,1,18,4,20,62)
names(shape_key) <- levels(lindata$ds)

line_key = c(2,2,1,1,1,1)
names(line_key) <- levels(lindata$ds)

# Benchmark-specific plot formatting
legend_pos=c(0.22,0.75)
y_name="Throughput (Mops/s)"
y_range_down = 0
if(t=="g0i50r50_thread")
  y_range_up = 16
else if (t=="g50i25r25_thread")
  y_range_up = 11
else
  y_range_up = 22

# Generate the plots
linchart<-ggplot(data=lindata,
                  aes(x=thread,y=mops,color=ds, shape=ds, linetype=ds))+
  geom_line()+xlab("Threads")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% lindata$ds])+
  scale_linetype_manual(values=line_key[names(line_key) %in% lindata$ds])+
  theme_bw()+ guides(shape=guide_legend(title=NULL))+ 
  guides(color=guide_legend(title=NULL))+
  guides(linetype=guide_legend(title=NULL))+
  scale_color_manual(values=color_key[names(color_key) %in% lindata$ds])+
  scale_x_continuous(breaks=c(0,10,20,30,40,50,60,70,80,90,100),
      minor_breaks=c(-10))+
  coord_cartesian(xlim = c(-2, 40), ylim = c(y_range_down,y_range_up))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 12))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.13,vjust = 13.5,margin = margin(t = 22, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./trees_",t,".pdf",sep=""),linchart,width=4, height = 4, units = "in", dpi=300, title = paste("trees_",t,".pdf",sep=""))
}
