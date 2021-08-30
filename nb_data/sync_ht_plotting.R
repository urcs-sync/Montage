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
tests<-c("g50i25r25_sync")
for (t in tests){
read.csv(paste("./",t,".csv",sep=""))->montagedata

montagedata$ds<-as.factor(gsub("PLockfreeHashTable","Izraelevitz",montagedata$ds))
montagedata$ds<-as.factor(gsub("cbnbMontageLfHashTable","nbMontage",montagedata$ds))
montagedata$ds<-as.factor(gsub("cbMontageLfHashTable","Montage",montagedata$ds))
montagedata$ds<-as.factor(gsub("hsnbMontageLfHashTable","nbMontage (hs)",montagedata$ds))
montagedata$ds<-as.factor(gsub("hsMontageLfHashTable","Montage (hs)",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVMLockfreeHashTable","NVM (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("LfHashTable","DRAM (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("Dali","Dalí",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVMSOFT","NVMSOFT",montagedata$ds))
montagedata$ds<-as.factor(gsub("SOFT","SOFT",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVTraverseHashTable","NVTraverse",montagedata$ds))
montagedata$ds<-as.factor(gsub("CLevelHashTable","CLevel",montagedata$ds))
d1<-subset(montagedata,ds=="DRAM (T)")
d2<-subset(montagedata,ds=="NVM (T)")
d3<-subset(montagedata,ds=="nbMontage")
d4<-subset(montagedata,ds=="Montage")
d5<-subset(montagedata,ds=="Izraelevitz")
d6<-subset(montagedata,ds=="Dalí")
d7<-subset(montagedata,ds=="SOFT")
d8<-subset(montagedata,ds=="NVMSOFT")
d9<-subset(montagedata,ds=="NVTraverse")
d10<-subset(montagedata,ds=="CLevel")
lkdata = rbind(d1,d2,d3,d4,d5,d6,d7,d8,d9,d10)

ddply(.data=lkdata,.(ds,sync),mutate,mops= mean(ops)/1000000)->lkdata
lindata = rbind(lkdata[,c("ds","sync","mops")])
lindata$ds <- factor(lindata$ds, levels=c("DRAM (T)", "NVM (T)", "nbMontage (hs)", "Montage (hs)", "nbMontage", "Montage", "Izraelevitz", "NVTraverse", "NVMSOFT", "SOFT", "CLevel", "Dalí"))

# Set up colors and shapes (invariant for all plots)
color_key = c("#12E1EA","#1245EA",
              "#C11B14","#FF69B4",
              "#C11B14","#FF69B4",
              "#809900","#1BC40F",
              "#FF8C00","#F86945",
              "#660099","#5947ff",
              "#191970")
names(color_key) <- levels(lindata$ds)

shape_key = c(2,1,18,4,18,4,20,62,15,0,17,3)
names(shape_key) <- levels(lindata$ds)

line_key = c(2,2,4,4,1,1,1,1,1,4,1,1)
names(line_key) <- levels(lindata$ds)

# Benchmark-specific plot formatting
legend_pos=c(0.52,0.78)
y_name="Throughput (Mops/s)"
y_range_down = 0
y_range_up = 24

# Generate the plots
linchart<-ggplot(data=lindata,
                  aes(x=sync,y=mops,color=ds, shape=ds, linetype=ds))+
  geom_line()+xlab("op/sync")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% lindata$ds])+
  scale_linetype_manual(values=line_key[names(line_key) %in% lindata$ds])+
  theme_bw()+ guides(shape=guide_legend(title=NULL,ncol=2))+ 
  guides(color=guide_legend(title=NULL,ncol=2))+
  guides(linetype=guide_legend(title=NULL,ncol=2))+
  scale_color_manual(values=color_key[names(color_key) %in% lindata$ds])+
  scale_x_continuous(trans='log2',label=scientific_10,breaks=c(1,10,100,1000,10000,100000,1000000))+
  coord_cartesian(xlim = c(0.8, 1000000), ylim = c(y_range_down,y_range_up))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(
    legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 12))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.095,vjust = 13.5,margin = margin(t = 22, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./sync.pdf",sep=""),linchart,width=6, height = 4, units = "in", dpi=300, title = paste("hashtables_",t,".pdf",sep=""))
}
