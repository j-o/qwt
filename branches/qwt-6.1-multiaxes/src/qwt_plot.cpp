/* -*- mode: C++ ; c-file-style: "stroustrup" -*- *****************************
 * Qwt Widget Library
 * Copyright (C) 1997   Josef Wilgen
 * Copyright (C) 2002   Uwe Rathmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the Qwt License, Version 1.0
 *****************************************************************************/

#include "qwt_plot.h"
#include "qwt_plot_dict.h"
#include "qwt_plot_layout.h"
#include "qwt_scale_widget.h"
#include "qwt_scale_engine.h"
#include "qwt_text_label.h"
#include "qwt_legend.h"
#include "qwt_legend_data.h"
#include "qwt_plot_canvas.h"
#include "qwt_scale_map_table.h"
#include <qmath.h>
#include <qpainter.h>
#include <qpointer.h>
#include <qpaintengine.h>
#include <qapplication.h>
#include <qevent.h>

static inline void qwtEnableLegendItems( QwtPlot *plot, bool on )
{
    if ( on )
    {
        QObject::connect( 
            plot, SIGNAL( legendDataChanged(
                const QVariant &, const QList<QwtLegendData> & ) ),
            plot, SLOT( updateLegendItems( 
                const QVariant &, const QList<QwtLegendData> & ) ) );
    }
    else
    {
        QObject::disconnect( 
            plot, SIGNAL( legendDataChanged(
                const QVariant &, const QList<QwtLegendData> & ) ),
            plot, SLOT( updateLegendItems( 
                const QVariant &, const QList<QwtLegendData> & ) ) );
    }
}

static void qwtSetTabOrder( 
    QWidget *first, QWidget *second, bool withChildren )
{
    QList<QWidget *> tabChain;
    tabChain += first;
    tabChain += second;

    if ( withChildren )
    {
        QList<QWidget *> children = second->findChildren<QWidget *>();

        QWidget *w = second->nextInFocusChain();
        while ( children.contains( w ) )
        {
            children.removeAll( w );

            tabChain += w;
            w = w->nextInFocusChain();
        }
    }

    for ( int i = 0; i < tabChain.size() - 1; i++ )
    {
        QWidget *from = tabChain[i];
        QWidget *to = tabChain[i+1];

        const Qt::FocusPolicy policy1 = from->focusPolicy();
        const Qt::FocusPolicy policy2 = to->focusPolicy();

        QWidget *proxy1 = from->focusProxy();
        QWidget *proxy2 = to->focusProxy();

        from->setFocusPolicy( Qt::TabFocus );
        from->setFocusProxy( NULL);

        to->setFocusPolicy( Qt::TabFocus );
        to->setFocusProxy( NULL);

        QWidget::setTabOrder( from, to );

        from->setFocusPolicy( policy1 );
        from->setFocusProxy( proxy1);

        to->setFocusPolicy( policy2 );
        to->setFocusProxy( proxy2 );
    }
}

QwtScaleMapTable qwtScaleMapTable( const QwtPlot *plot ) 
{
    QwtScaleMapTable table;
          
    for ( int axisPos = 0; axisPos < QwtPlot::NumAxisPositions; axisPos++ )
    {   
        for ( int i = 0; i < plot->axesCount( axisPos ); i++ )
            table.maps[axisPos] += plot->canvasMap( QwtAxisId( axisPos, i ) );
    }

    return table;
}

class QwtPlot::PrivateData
{
public:
    QPointer<QwtTextLabel> titleLabel;
    QPointer<QwtTextLabel> footerLabel;
    QPointer<QWidget> canvas;
    QPointer<QwtAbstractLegend> legend;
    QwtPlotLayout *layout;

    bool autoReplot;
};

/*!
  \brief Constructor
  \param parent Parent widget
 */
QwtPlot::QwtPlot( QWidget *parent ):
    QFrame( parent )
{
    initPlot( QwtText() );
}

/*!
  \brief Constructor
  \param title Title text
  \param parent Parent widget
 */
QwtPlot::QwtPlot( const QwtText &title, QWidget *parent ):
    QFrame( parent )
{
    initPlot( title );
}

//! Destructor
QwtPlot::~QwtPlot()
{
    detachItems( QwtPlotItem::Rtti_PlotItem, autoDelete() );

    delete d_data->layout;
    deleteScaleData();
    delete d_data;
}

/*!
  \brief Initializes a QwtPlot instance
  \param title Title text
 */
void QwtPlot::initPlot( const QwtText &title )
{
    d_data = new PrivateData;

    d_data->layout = new QwtPlotLayout;
    d_data->autoReplot = false;

    // title
    d_data->titleLabel = new QwtTextLabel( this );
    d_data->titleLabel->setObjectName( "QwtPlotTitle" );
    d_data->titleLabel->setFont( QFont( fontInfo().family(), 14, QFont::Bold ) );

    QwtText text( title );
    text.setRenderFlags( Qt::AlignCenter | Qt::TextWordWrap );
    d_data->titleLabel->setText( text );

    // footer
    d_data->footerLabel = new QwtTextLabel( this );
    d_data->footerLabel->setObjectName( "QwtPlotFooter" );

    QwtText footer;
    footer.setRenderFlags( Qt::AlignCenter | Qt::TextWordWrap );
    d_data->footerLabel->setText( footer );

    // legend
    d_data->legend = NULL;

    // scales
    initScaleData();

    // canvas
    d_data->canvas = new QwtPlotCanvas( this );
    d_data->canvas->setObjectName( "QwtPlotCanvas" );
    d_data->canvas->installEventFilter( this );

    setSizePolicy( QSizePolicy::MinimumExpanding,
        QSizePolicy::MinimumExpanding );

    resize( 200, 200 );

    QList<QWidget *> focusChain;
    focusChain << this << d_data->titleLabel;

    for ( int i = 0; i < axesCount( xTop ); i++ )
        focusChain << axisWidget( QwtAxisId( xTop, i ) );

    for ( int i = 0; i < axesCount( yLeft ); i++ )
        focusChain << axisWidget( QwtAxisId( yLeft, i ) );

    focusChain << d_data->canvas;

    for ( int i = 0; i < axesCount( yRight ); i++ )
        focusChain << axisWidget( QwtAxisId( yRight, i ) );

    for ( int i = 0; i < axesCount( xBottom ); i++ )
        focusChain << axisWidget( QwtAxisId( xBottom, i ) );

    focusChain << d_data->footerLabel;

    for ( int i = 0; i < focusChain.size() - 1; i++ )
        qwtSetTabOrder( focusChain[i], focusChain[i+1], false );

    qwtEnableLegendItems( this, true );
}

/*!
  \brief Set the drawing canvas of the plot widget

  QwtPlot invokes methods of the canvas as meta methods ( see QMetaObject ).
  In opposite to using conventional C++ techniques like virtual methods
  they allow to use canvas implementations that are derived from 
  QWidget or QGLWidget.

  The following meta methods could be implemented:

  - replot()
    When the canvas doesn't offer a replot method, QwtPlot calls
    update() instead.

  - borderPath()
    The border path is necessary to clip the content of the canvas
    When the canvas doesn't have any special border ( f.e rounded corners )
    it is o.k. not to implement this method.

  The default canvas is a QwtPlotCanvas 

  \param canvas Canvas Widget
  \sa canvas()
 */
void QwtPlot::setCanvas( QWidget *canvas )
{
    if ( canvas == d_data->canvas )
        return;

    delete d_data->canvas;
    d_data->canvas = canvas;

    if ( canvas )
    {
        canvas->setParent( this );
        canvas->installEventFilter( this );

        if ( isVisible() )
            canvas->show();
    }
}

/*!
  \brief Adds handling of layout requests
  \param event Event

  \return See QFrame::event()
*/
bool QwtPlot::event( QEvent *event )
{
    bool ok = QFrame::event( event );
    switch ( event->type() )
    {
        case QEvent::LayoutRequest:
            updateLayout();
            break;
        case QEvent::PolishRequest:
            replot();
            break;
        default:;
    }
    return ok;
}

/*!
  \brief Event filter

  The plot handles the following events for the canvas:

  - QEvent::Resize
    The canvas margins might depend on its size

  - QEvent::ContentsRectChange
    The layout needs to be recalculated

  \param object Object to be filtered
  \param event Event

  \return See QFrame::eventFilter()

  \sa updateCanvasMargins(), updateLayout()
*/
bool QwtPlot::eventFilter( QObject *object, QEvent *event )
{
    if ( object == d_data->canvas )
    {
        if ( event->type() == QEvent::Resize )
        {
            updateCanvasMargins();
        }
        else if ( event->type() == QEvent::ContentsRectChange )
        {
            updateLayout();
        }
    }

    return QFrame::eventFilter( object, event );
}

//! Replots the plot if autoReplot() is \c true.
void QwtPlot::autoRefresh()
{
    if ( d_data->autoReplot )
        replot();
}

/*!
  \brief Set or reset the autoReplot option

  If the autoReplot option is set, the plot will be
  updated implicitly by manipulating member functions.
  Since this may be time-consuming, it is recommended
  to leave this option switched off and call replot()
  explicitly if necessary.

  The autoReplot option is set to false by default, which
  means that the user has to call replot() in order to make
  changes visible.
  \param tf \c true or \c false. Defaults to \c true.
  \sa replot()
*/
void QwtPlot::setAutoReplot( bool tf )
{
    d_data->autoReplot = tf;
}

/*! 
  \return true if the autoReplot option is set.
  \sa setAutoReplot()
*/
bool QwtPlot::autoReplot() const
{
    return d_data->autoReplot;
}

/*!
  Change the plot's title
  \param title New title
*/
void QwtPlot::setTitle( const QString &title )
{
    if ( title != d_data->titleLabel->text().text() )
    {
        d_data->titleLabel->setText( title );
        updateLayout();
    }
}

/*!
  Change the plot's title
  \param title New title
*/
void QwtPlot::setTitle( const QwtText &title )
{
    if ( title != d_data->titleLabel->text() )
    {
        d_data->titleLabel->setText( title );
        updateLayout();
    }
}

//! \return Title of the plot
QwtText QwtPlot::title() const
{
    return d_data->titleLabel->text();
}

//! \return Title label widget.
QwtTextLabel *QwtPlot::titleLabel()
{
    return d_data->titleLabel;
}

//! \return Title label widget.
const QwtTextLabel *QwtPlot::titleLabel() const
{
    return d_data->titleLabel;
}

/*!
  Change the text the footer 
  \param text New text of the footer
*/
void QwtPlot::setFooter( const QString &text )
{
    if ( text != d_data->footerLabel->text().text() )
    {
        d_data->footerLabel->setText( text );
        updateLayout();
    }
}

/*!
  Change the text the footer 
  \param text New text of the footer
*/
void QwtPlot::setFooter( const QwtText &text )
{
    if ( text != d_data->footerLabel->text() )
    {
        d_data->footerLabel->setText( text );
        updateLayout();
    }
}

//! \return Text of the footer
QwtText QwtPlot::footer() const
{
    return d_data->footerLabel->text();
}

//! \return Footer label widget.
QwtTextLabel *QwtPlot::footerLabel()
{
    return d_data->footerLabel;
}

//! \return Footer label widget.
const QwtTextLabel *QwtPlot::footerLabel() const
{
    return d_data->footerLabel;
}

/*!
   \brief Assign a new plot layout

   \param layout Layout()
   \sa plotLayout()
 */
void QwtPlot::setPlotLayout( QwtPlotLayout *layout )
{
    if ( layout != d_data->layout )
    {
        delete d_data->layout;
        layout = d_data->layout;

        updateLayout();
    }
}

//! \return the plot's layout
QwtPlotLayout *QwtPlot::plotLayout()
{
    return d_data->layout;
}

//! \return the plot's layout
const QwtPlotLayout *QwtPlot::plotLayout() const
{
    return d_data->layout;
}

/*!
  \return the plot's legend
  \sa insertLegend()
*/
QwtAbstractLegend *QwtPlot::legend()
{
    return d_data->legend;
}

/*!
  \return the plot's legend
  \sa insertLegend()
*/
const QwtAbstractLegend *QwtPlot::legend() const
{
    return d_data->legend;
}


/*!
  \return the plot's canvas
*/
QWidget *QwtPlot::canvas()
{
    return d_data->canvas;
}

/*!
  \return the plot's canvas
*/
const QWidget *QwtPlot::canvas() const
{
    return d_data->canvas;
}

/*!
  \return Size hint for the plot widget
  \sa minimumSizeHint()
*/
QSize QwtPlot::sizeHint() const
{
    int dw = 0;
    int dh = 0;

    for ( int axisPos = 0; axisPos < NumAxisPositions; axisPos++ )
    {
        for ( int i = 0; i < axesCount( axisPos ); i++ )
        {
            const QwtAxisId axisId( axisPos, i );

            if ( isAxisVisible( axisId ) )
            {
                const int niceDist = 40;
                const QwtScaleWidget *scaleWidget = axisWidget( axisId );
                const QwtScaleDiv &scaleDiv = scaleWidget->scaleDraw()->scaleDiv();
                const int majCnt = scaleDiv.ticks( QwtScaleDiv::MajorTick ).count();

                const QSize hint = scaleWidget->minimumSizeHint();

                if ( axisPos == yLeft || axisPos == yRight )
                {
                    const int hDiff = ( majCnt - 1 ) * niceDist - hint.height();
                    dh = qMax( dh, hDiff );
                }
                else
                {
                    const int wDiff = ( majCnt - 1 ) * niceDist - hint.width();
                    dw = qMax( dw, wDiff );
                }
            }
        }
    }

    return minimumSizeHint() + QSize( dw, dh );
}

/*!
  \brief Return a minimum size hint
*/
QSize QwtPlot::minimumSizeHint() const
{
    QSize hint = d_data->layout->minimumSizeHint( this );
    hint += QSize( 2 * frameWidth(), 2 * frameWidth() );

    return hint;
}

/*!
  Resize and update internal layout
  \param e Resize event
*/
void QwtPlot::resizeEvent( QResizeEvent *e )
{
    QFrame::resizeEvent( e );
    updateLayout();
}

/*!
  \brief Redraw the plot

  If the autoReplot option is not set (which is the default)
  or if any curves are attached to raw data, the plot has to
  be refreshed explicitly in order to make changes visible.

  \sa updateAxes(), setAutoReplot()
*/
void QwtPlot::replot()
{
    bool doAutoReplot = autoReplot();
    setAutoReplot( false );

    updateAxes();

    /*
      Maybe the layout needs to be updated, because of changed
      axes labels. We need to process them here before painting
      to avoid that scales and canvas get out of sync.
     */
    QApplication::sendPostedEvents( this, QEvent::LayoutRequest );

    if ( d_data->canvas )
    {
        const bool ok = QMetaObject::invokeMethod( 
            d_data->canvas, "replot", Qt::DirectConnection );
        if ( !ok )
        {
            // fallback, when canvas has no a replot method
            d_data->canvas->update( d_data->canvas->contentsRect() );
        }
    }

    setAutoReplot( doAutoReplot );
}

/*!
  \brief Adjust plot content to its current size.
  \sa resizeEvent()
*/
void QwtPlot::updateLayout()
{
    d_data->layout->update( this, contentsRect() );

    const QRect titleRect = d_data->layout->titleRect().toRect();
    const QRect footerRect = d_data->layout->footerRect().toRect();
    const QRect legendRect = d_data->layout->legendRect().toRect();
    const QRect canvasRect = d_data->layout->canvasRect().toRect();

    // resize and show the visible widgets

    if ( !d_data->titleLabel->text().isEmpty() )
    {
        d_data->titleLabel->setGeometry( titleRect );
        if ( !d_data->titleLabel->isVisibleTo( this ) )
            d_data->titleLabel->show();
    }
    else
        d_data->titleLabel->hide();

    if ( !d_data->footerLabel->text().isEmpty() )
    {
        d_data->footerLabel->setGeometry( footerRect );
        if ( !d_data->footerLabel->isVisibleTo( this ) )
            d_data->footerLabel->show();
    }
    else
        d_data->footerLabel->hide();

    for ( int axisPos = 0; axisPos < NumAxisPositions; axisPos++ )
    {
        for ( int i = 0; i < axesCount( axisPos ); i++ )
        {
            const QwtAxisId axisId( axisPos, i );

            QwtScaleWidget *scaleWidget = axisWidget( axisId );
            if ( isAxisVisible( axisId ) )
            {
                const QRect scaleRect = d_data->layout->scaleRect( axisId ).toRect();
                scaleWidget->setGeometry( scaleRect );

                if ( !scaleWidget->isVisibleTo( this ) )
                    scaleWidget->show();
            }
            else
            {
                scaleWidget->hide();
            }
        }
    }

    if ( d_data->legend )
    {
        if ( d_data->legend->isEmpty() )
        {
            d_data->legend->hide();
        }
        else
        {
            d_data->legend->setGeometry( legendRect );
            d_data->legend->show();
        }
    }

    d_data->canvas->setGeometry( canvasRect );
}

/*!
  \brief Calculate the canvas margins

  \param maps QwtPlot::axisCnt maps, mapping between plot and paint device coordinates
  \param canvasRect Bounding rectangle where to paint
  \param left Return parameter for the left margin
  \param top Return parameter for the top margin
  \param right Return parameter for the right margin
  \param bottom Return parameter for the bottom margin

  Plot items might indicate, that they need some extra space
  at the borders of the canvas by the QwtPlotItem::Margins flag.

  updateCanvasMargins(), QwtPlotItem::getCanvasMarginHint()
 */
void QwtPlot::getCanvasMarginsHint(
    const QwtScaleMapTable& mapsTable, const QRectF &canvasRect,
    double &left, double &top, double &right, double &bottom) const
{
    left = top = right = bottom = -1.0;

    const QwtPlotItemList& itmList = itemList();
    for ( QwtPlotItemIterator it = itmList.begin();
        it != itmList.end(); ++it )
    {
        const QwtPlotItem *item = *it;
        if ( item->testItemAttribute( QwtPlotItem::Margins ) &&
            mapsTable.isValid( item->xAxis() ) &&
            mapsTable.isValid( item->yAxis() ) )
        {
            double m[ QwtPlot::NumAxisPositions ];
            item->getCanvasMarginHint(
                mapsTable.map( item->xAxis() ), 
                mapsTable.map( item->yAxis() ),
                canvasRect, m[yLeft], m[xTop], m[yRight], m[xBottom] );

            left = qMax( left, m[yLeft] );
            top = qMax( top, m[xTop] );
            right = qMax( right, m[yRight] );
            bottom = qMax( bottom, m[xBottom] );
        }
    }
}

/*!
  \brief Update the canvas margins

  Plot items might indicate, that they need some extra space
  at the borders of the canvas by the QwtPlotItem::Margins flag.

  getCanvasMarginsHint(), QwtPlotItem::getCanvasMarginHint()
 */
void QwtPlot::updateCanvasMargins()
{
    double margins[NumAxisPositions];
    getCanvasMarginsHint( qwtScaleMapTable( this ), canvas()->contentsRect(),
        margins[yLeft], margins[xTop], margins[yRight], margins[xBottom] );
    
    bool doUpdate = false;
    for ( int axisPos = 0; axisPos < NumAxisPositions; axisPos++ )
    {
        if ( margins[axisPos] >= 0.0 )
        {
            const int m = qCeil( margins[axisPos] );
            plotLayout()->setCanvasMargin( m, axisPos);
            doUpdate = true;
        }
    }

    if ( doUpdate )
        updateLayout();
}

/*!
  Redraw the canvas.
  \param painter Painter used for drawing

  \warning drawCanvas calls drawItems what is also used
           for printing. Applications that like to add individual
           plot items better overload drawItems()
  \sa drawItems()
*/
void QwtPlot::drawCanvas( QPainter *painter )
{
    const QwtScaleMapTable table = qwtScaleMapTable( this );
    drawItems( painter, d_data->canvas->contentsRect(), table );
}

/*!
  Redraw the canvas items.

  \param painter Painter used for drawing
  \param canvasRect Bounding rectangle where to paint
  \param maps QwtPlot::axisCnt maps, mapping between plot and paint device coordinates

  \note Usually canvasRect is contentsRect() of the plot canvas.
        Due to a bug in Qt this rectangle might be wrong for certain 
        frame styles ( f.e QFrame::Box ) and it might be necessary to 
        fix the margins manually using QWidget::setContentsMargins()
*/

void QwtPlot::drawItems( QPainter *painter, 
    const QRectF &canvasRect, const QwtScaleMapTable &mapTable ) const
{
    const QwtPlotItemList& itmList = itemList();
    for ( QwtPlotItemIterator it = itmList.begin();
        it != itmList.end(); ++it )
    {
        QwtPlotItem *item = *it;
        if ( item && item->isVisible() )
        {
            if ( mapTable.isValid( item->xAxis() ) && 
                mapTable.isValid( item->yAxis() ) )
            {
                painter->save();

                painter->setRenderHint( QPainter::Antialiasing,
                    item->testRenderHint( QwtPlotItem::RenderAntialiased ) );
                painter->setRenderHint( QPainter::HighQualityAntialiasing,
                    item->testRenderHint( QwtPlotItem::RenderAntialiased ) );

                item->draw( painter, mapTable.map( item->xAxis() ),
                    mapTable.map( item->yAxis() ), canvasRect );

                painter->restore();
            }
        }
    }
}

/*!
  \param axisPos Axis
  \return Map for the axis on the canvas. With this map pixel coordinates can
          translated to plot coordinates and vice versa.
  \sa QwtScaleMap, transform(), invTransform()

*/
QwtScaleMap QwtPlot::canvasMap( QwtAxisId axisId ) const
{
    QwtScaleMap map;
    if ( !d_data->canvas )
        return map;

    map.setTransformation( axisScaleEngine( axisId )->transformation() );

    const QwtScaleDiv &sd = axisScaleDiv( axisId );
    map.setScaleInterval( sd.lowerBound(), sd.upperBound() );

    if ( isAxisVisible( axisId ) )
    {
        const QwtScaleWidget *s = axisWidget( axisId );
        if ( axisId.pos == yLeft || axisId.pos == yRight )
        {
            double y = s->y() + s->startBorderDist() - d_data->canvas->y();
            double h = s->height() - s->startBorderDist() - s->endBorderDist();
            map.setPaintInterval( y + h, y );
        }
        else
        {
            double x = s->x() + s->startBorderDist() - d_data->canvas->x();
            double w = s->width() - s->startBorderDist() - s->endBorderDist();
            map.setPaintInterval( x, x + w );
        }
    }
    else
    {
        int margin = 0;
        if ( !plotLayout()->alignCanvasToScale( axisId.pos ) )
            margin = plotLayout()->canvasMargin( axisId.pos );

        const QRect &canvasRect = d_data->canvas->contentsRect();
        if ( axisId.pos == yLeft || axisId.pos == yRight )
        {
            map.setPaintInterval( canvasRect.bottom() - margin,
                canvasRect.top() + margin );
        }
        else
        {
            map.setPaintInterval( canvasRect.left() + margin,
                canvasRect.right() - margin );
        }
    }
    return map;
}

/*!
  \brief Change the background of the plotting area

  Sets brush to QPalette::Window of all color groups of
  the palette of the canvas. Using canvas()->setPalette()
  is a more powerful way to set these colors.

  \param brush New background brush
  \sa canvasBackground()
*/
void QwtPlot::setCanvasBackground( const QBrush &brush )
{
    QPalette pal = d_data->canvas->palette();
    pal.setBrush( QPalette::Window, brush );

    canvas()->setPalette( pal );
}

/*!
  Nothing else than: canvas()->palette().brush(
        QPalette::Normal, QPalette::Window);

  \return Background brush of the plotting area.
  \sa setCanvasBackground()
*/
QBrush QwtPlot::canvasBackground() const
{
    return canvas()->palette().brush(
        QPalette::Normal, QPalette::Window );
}

/*!
  \brief Insert a legend

  If the position legend is \c QwtPlot::LeftLegend or \c QwtPlot::RightLegend
  the legend will be organized in one column from top to down.
  Otherwise the legend items will be placed in a table
  with a best fit number of columns from left to right.

  insertLegend() will set the plot widget as parent for the legend.
  The legend will be deleted in the destructor of the plot or when 
  another legend is inserted.

  Legends, that are not inserted into the layout of the plot widget
  need to connect to the legendDataChanged() signal. Calling updateLegend()
  initiates this signal for an initial update. When the application code
  wants to implement its own layout this also needs to be done for
  rendering plots to a document ( see QwtPlotRenderer ).

  \param legend Legend
  \param pos The legend's position. For top/left position the number
             of columns will be limited to 1, otherwise it will be set to
             unlimited.

  \param ratio Ratio between legend and the bounding rectangle
               of title, canvas and axes. The legend will be shrunk
               if it would need more space than the given ratio.
               The ratio is limited to ]0.0 .. 1.0]. In case of <= 0.0
               it will be reset to the default ratio.
               The default vertical/horizontal ratio is 0.33/0.5.

  \sa legend(), QwtPlotLayout::legendPosition(),
      QwtPlotLayout::setLegendPosition()
*/
void QwtPlot::insertLegend( QwtAbstractLegend *legend,
    QwtPlot::LegendPosition pos, double ratio )
{
    d_data->layout->setLegendPosition( pos, ratio );

    if ( legend != d_data->legend )
    {
        if ( d_data->legend && d_data->legend->parent() == this )
            delete d_data->legend;

        d_data->legend = legend;

        if ( d_data->legend )
        {
            connect( this, 
                SIGNAL( legendDataChanged( 
                    const QVariant &, const QList<QwtLegendData> & ) ),
                d_data->legend, 
                SLOT( updateLegend( 
                    const QVariant &, const QList<QwtLegendData> & ) ) 
            );

            if ( d_data->legend->parent() != this )
                d_data->legend->setParent( this );

            qwtEnableLegendItems( this, false );
            updateLegend();
            qwtEnableLegendItems( this, true );

            QwtLegend *lgd = qobject_cast<QwtLegend *>( legend );
            if ( lgd )
            {
                switch ( d_data->layout->legendPosition() )
                {
                    case LeftLegend:
                    case RightLegend:
                    {
                        if ( lgd->maxColumns() == 0     )
                            lgd->setMaxColumns( 1 ); // 1 column: align vertical
                        break;
                    }
                    case TopLegend:
                    case BottomLegend:
                    {
                        lgd->setMaxColumns( 0 ); // unlimited
                        break;
                    }
                    default:
                        break;
                }
            }

            QWidget *previousInChain = NULL;
            switch ( d_data->layout->legendPosition() )
            {
                case LeftLegend:
                {
                    previousInChain = axisWidget( 
                        QwtAxisId( QwtPlot::xTop, axesCount( QwtPlot::xTop ) - 1 ) );
                    break;
                }
                case TopLegend:
                {
                    previousInChain = this;
                    break;
                }
                case RightLegend:
                {
                    previousInChain = axisWidget( 
                        QwtAxisId( QwtPlot::yRight, axesCount( QwtPlot::yRight ) - 1 ) );
                    break;
                }
                case BottomLegend:
                {
                    previousInChain = footerLabel();
                    break;
                }
            }

            if ( previousInChain )
                qwtSetTabOrder( previousInChain, legend, true );
        }
    }

    updateLayout();
}

/*!
  Emit legendDataChanged() for all plot item

  \sa QwtPlotItem::legendData(), legendDataChanged()
 */
void QwtPlot::updateLegend()
{
    const QwtPlotItemList& itmList = itemList();
    for ( QwtPlotItemIterator it = itmList.begin();
        it != itmList.end(); ++it )
    {
        updateLegend( *it );
    }
}

/*!
  Emit legendDataChanged() for a plot item

  \param plotItem Plot item
  \sa QwtPlotItem::legendData(), legendDataChanged()
 */
void QwtPlot::updateLegend( const QwtPlotItem *plotItem )
{
    if ( plotItem == NULL )
        return;

    QList<QwtLegendData> legendData;

    if ( plotItem->testItemAttribute( QwtPlotItem::Legend ) )
        legendData = plotItem->legendData();

    const QVariant itemInfo = itemToInfo( const_cast< QwtPlotItem *>( plotItem) );
    Q_EMIT legendDataChanged( itemInfo, legendData );
}

/*!
  \brief Update all plot items interested in legend attributes

  Call QwtPlotItem::updateLegend(), when the QwtPlotItem::LegendInterest
  flag is set.

  \param itemInfo Info about the plot item
  \param legendData Entries to be displayed for the plot item ( usually 1 )

  \sa QwtPlotItem::LegendInterest,
      QwtPlotLegendItem, QwtPlotItem::updateLegend()
 */
void QwtPlot::updateLegendItems( const QVariant &itemInfo,
    const QList<QwtLegendData> &legendData )
{
    QwtPlotItem *plotItem = infoToItem( itemInfo );
    if ( plotItem )
    {
        const QwtPlotItemList& itmList = itemList();
        for ( QwtPlotItemIterator it = itmList.begin();
            it != itmList.end(); ++it )
        {
            QwtPlotItem *item = *it;
            if ( item->testItemInterest( QwtPlotItem::LegendInterest ) )
                item->updateLegend( plotItem, legendData );
        }
    }
}

/*!
  \brief Attach/Detach a plot item 

  \param plotItem Plot item
  \param on When true attach the item, otherwise detach it
 */
void QwtPlot::attachItem( QwtPlotItem *plotItem, bool on )
{
    if ( plotItem->testItemInterest( QwtPlotItem::LegendInterest ) )
    {
        // plotItem is some sort of legend

        const QwtPlotItemList& itmList = itemList();
        for ( QwtPlotItemIterator it = itmList.begin();
            it != itmList.end(); ++it )
        {
            QwtPlotItem *item = *it;

            QList<QwtLegendData> legendData;
            if ( on && item->testItemAttribute( QwtPlotItem::Legend ) )
            {
                legendData = item->legendData();
                plotItem->updateLegend( item, legendData );
            }
        }
    }

    if ( on )
        insertItem( plotItem );
    else 
        removeItem( plotItem );

    Q_EMIT itemAttached( plotItem, on );

    if ( plotItem->testItemAttribute( QwtPlotItem::Legend ) )
    {
        // the item wants to be represented on the legend

        if ( on )
        {
            updateLegend( plotItem );
        }
        else
        {
            const QVariant itemInfo = itemToInfo( plotItem );
            Q_EMIT legendDataChanged( itemInfo, QList<QwtLegendData>() );
        }
    }

    if ( autoReplot() )
        update();
}

/*!
  \brief Build an information, that can be used to identify
         a plot item on the legend.

  The default implementation simply wraps the plot item
  into a QVariant object. When overloading itemToInfo()
  usually infoToItem() needs to reimplemeted too.

\code
    QVariant itemInfo;
    qVariantSetValue( itemInfo, plotItem );
\endcode

  \param plotItem Plot item
  \return Plot item embedded in a QVariant
  \sa infoToItem()
 */
QVariant QwtPlot::itemToInfo( QwtPlotItem *plotItem ) const
{
    QVariant itemInfo;
    qVariantSetValue( itemInfo, plotItem );

    return itemInfo;
}

/*!
  \brief Identify the plot item according to an item info object,
         that has bee generated from itemToInfo().

  The default implementation simply tries to unwrap a QwtPlotItem 
  pointer:

\code
    if ( itemInfo.canConvert<QwtPlotItem *>() )
        return qvariant_cast<QwtPlotItem *>( itemInfo );
\endcode
  \param itemInfo Plot item
  \return A plot item, when successful, otherwise a NULL pointer.
  \sa itemToInfo()
*/
QwtPlotItem *QwtPlot::infoToItem( const QVariant &itemInfo ) const
{
    if ( itemInfo.canConvert<QwtPlotItem *>() )
        return qvariant_cast<QwtPlotItem *>( itemInfo );

    return NULL;
}

