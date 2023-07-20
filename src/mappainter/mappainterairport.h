/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#ifndef LITTLENAVMAP_MAPPAINTERAIRPORT_H
#define LITTLENAVMAP_MAPPAINTERAIRPORT_H

#include "mappainter/mappainter.h"

#include "fs/common/xpgeometry.h"

class SymbolPainter;

namespace map {
struct MapAirport;

struct MapApron;

struct MapRunway;

}

struct PaintAirportType;

/*
 * Draws airport symbols, runway overview and complete airport diagram. Airport details are also drawn for
 * the flight plan.
 */
class MapPainterAirport :
  public MapPainter
{
  Q_DECLARE_TR_FUNCTIONS(MapPainter)

public:
  MapPainterAirport(MapPaintWidget *mapPaintWidget, MapScale *mapScale, PaintContext *paintContext);
  virtual ~MapPainterAirport() override;

  virtual void render() override;

private:
  void drawAirportSymbol(const map::MapAirport& ap, float x, float y, float size);
  void drawAirportDiagram(const map::MapAirport& airport);
  void drawAirportDiagramBackground(const map::MapAirport& airport);
  void drawAirportSymbolOverview(const map::MapAirport& ap, float x, float y, float symsize);
  void runwayCoords(const QList<map::MapRunway> *runways, QList<QPointF>* centers, QList<QRectF>* rects,
                    QList<QRectF>* innerRects, QList<QRectF>* outlineRects, bool overview);
  void drawFsApron(const map::MapApron& apron);
  void drawXplaneApron(const map::MapApron& apron, bool fast);

};

#endif // LITTLENAVMAP_MAPPAINTERAIRPORT_H
